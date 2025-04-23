#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <poll.h>
#include <pthread.h> 

#include "utils_v3.h"
#include "messages.h"
#include "game.h"  

#define BACKLOG 5
#define MAP_FILE "resources/map.txt"

typedef struct {
    int count;
    int fds[MAX_PLAYERS];
    int sem;
    struct GameState game_state;
} SharedData;

typedef struct {
    int broadcast_pipe[2];  
    SharedData* shared;     // Référence vers la mémoire partagée
} BroadcastData;

volatile sig_atomic_t stop_requested = 0;

int initSocketServer(int serverPort) {
    int socketfd = ssocket();
    sbind(serverPort, socketfd);
    slisten(socketfd, BACKLOG);
    return socketfd;
}

void* broadcast_handler(void* arg) {
    int pipe_read = *(int*)arg;
    char buffer[1024];
    
    while (1) {
        ssize_t n = read(pipe_read, buffer, sizeof(buffer));
        if (n <= 0) break;
        
        // Envoie à tous les clients
        for (int i = 0; i < shared->count; i++) {
            nwrite(shared->fds[i], buffer, n);
        }
    }
    return NULL;
}

void client_handler(int client_fd, SharedData* shared) {
    StructMessage msg;
    
    // 1. INSCRIPTION
    if (sread(client_fd, &msg, sizeof(msg)) <= 0) {
        perror("Erreur lecture pseudo");
        sclose(client_fd);
        exit(EXIT_FAILURE);
    }

    printf("[FILS %d] Joueur inscrit : %s\n", getpid(), msg.messageText);

    // Réponse OK
    msg.code = INSCRIPTION_OK;
    nwrite(client_fd, &msg, sizeof(msg));

    // Ajout synchronisé du client
    sem_down0(shared->sem);
    shared->fds[shared->count] = client_fd;
    int player_id = shared->count++;
    sem_up0(shared->sem);

    // 2. ATTENTE DEBUT PARTIE
    while (shared->count < MAX_PLAYERS) {
        sleep(1);
    }

    // 3. NOTIFICATION DEBUT PARTIE
    msg.code = GAME_START;
    snprintf(msg.messageText, sizeof(msg.messageText), "%d", player_id);
    nwrite(client_fd, &msg, sizeof(msg));

    // 4. BOUCLE DE COMMUNICATION
    while (1) {
        // Lecture des commandes du client
        if (sread(client_fd, &msg, sizeof(msg)) <= 0) {
            break;
        }

        // Traitement des commandes (sera implémenté plus tard)
        if (msg.code == PLAYER_MOVE) {
            sem_down0(shared->sem);
            // Traiter le mouvement ici
            sem_up0(shared->sem);
        }
    }

    sclose(client_fd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {

    int sockfd = initSocketServer(SERVER_PORT);
    printf("Serveur en écoute sur le port %d\n", SERVER_PORT);

    // 1. CREATION DES RESSOURCES PARTAGEES
    int shm_id = sshmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0600);
    SharedData* shared = sshmat(shm_id);
    memset(shared, 0, sizeof(SharedData));
    // Création du pipe de broadcast
    int broadcast_pipe[2];
    spipe(broadcast_pipe);

    // Initialisation sémaphore
    shared->sem = sem_create(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | 0600, 1);

    // Structure pour le thread de broadcast
    BroadcastData broadcast_data = {
        .broadcast_pipe = {broadcast_pipe[0], broadcast_pipe[1]},
        .shared = shared
    };

    // 2. Chargement de la map AVEC broadcast
    FileDescriptor map_fd = sopen(MAP_FILE, O_RDONLY, 0);
    load_map(map_fd, broadcast_pipe[1], &shared->game_state);
    sclose(map_fd);
    printf("Map chargée avec succès\n");

    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, broadcast_handler, &broadcast_pipe[0]);

    // 3. BOUCLE PRINCIPALE
    while (!stop_requested) {
        int client_fd = accept(sockfd, NULL, NULL);
        if (client_fd < 0) {
            if (stop_requested) break;
            perror("Erreur accept");
            continue;
        }

        if (shared->count >= MAX_PLAYERS) {
            StructMessage msg;
            msg.code = INSCRIPTION_KO;
            nwrite(client_fd, &msg, sizeof(msg));
            sclose(client_fd);
            continue;
        }

        pid_t pid = sfork();
        if (pid == 0) {
            sclose(sockfd);
            client_handler(client_fd, shared);
            sshmdt(shared);
            exit(EXIT_SUCCESS);
        } else if (pid > 0) {
            sclose(client_fd);
        } else {
            perror("Erreur sfork");
            sclose(client_fd);
        }
    }

    // 4. NETTOYAGE
    printf("Serveur en arrêt...\n");
    sclose(sockfd);
    sem_delete(shared->sem);
    sshmdt(shared);
    shmctl(shm_id, IPC_RMID, NULL);
    sclose(broadcast_pipe[1]);  // Ferme l'écriture pour terminer le thread
    pthread_join(broadcast_thread, NULL);
    sclose(broadcast_pipe[0]);
    
    return 0;
}
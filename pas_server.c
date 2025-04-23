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
#define MAP_FILE "resources/map1.txt"

typedef struct {
    int count;
    int fds[MAX_PLAYERS];
    int sem;
    struct GameState game_state;
} SharedData;

typedef struct {
    int pipe_read;
    SharedData* shared;
} BroadcastThreadData;

volatile sig_atomic_t stop_requested = 0;

int initSocketServer(int serverPort) {
    int socketfd = ssocket();
    sbind(serverPort, socketfd);
    slisten(socketfd, BACKLOG);
    return socketfd;
}

const char* item_to_string(enum Item item) {
    switch (item) {
        case WALL:      return "WALL";
        case FLOOR:     return "FLOOR";
        case FOOD:      return "FOOD";
        case SUPERFOOD: return "SUPERFOOD";
        case PLAYER1:   return "PLAYER1";
        case PLAYER2:   return "PLAYER2";
        default:        return "UNKNOWN";
    }
}



void* broadcast_handler(void* arg) {
    BroadcastThreadData* data = (BroadcastThreadData*)arg;
    char buffer[1024];
    
    printf("[BROADCAST] Thread démarré (pipe_read=%d)\n", data->pipe_read);
    
    while (1) {
        ssize_t n = read(data->pipe_read, buffer, sizeof(buffer));
        if (n <= 0) {
            printf("[BROADCAST] Fin du pipe (n=%zd)\n", n);
            break;
        }
        
        printf("[BROADCAST] Reçu %zd bytes: type=%d\n", n, ((union Message*)buffer)->msgt);
        
        sem_down0(data->shared->sem);
        // Dans broadcast_handler
        printf("[BROADCAST] Envoi à %d clients\n", data->shared->count);
        for (int i = 0; i < data->shared->count; i++) {
            if (data->shared->fds[i] > 0) {
                printf("Envoi au client %d (fd=%d)\n", i, data->shared->fds[i]);
                union Message* m = (union Message*)buffer;
                printf("Message type: %d\n", m->msgt);
                nwrite(data->shared->fds[i], buffer, n);
            }
        }
        
        StructMessage* m = (StructMessage*) buffer;

        if (m->code == GAME_START) {
            printf("[BROADCAST] Envoi MAP_DATA aux clients\n");
            // Send the actual map data
            StructMessage map_msg;
            map_msg.code = MAP_DATA;
            const char *map_str = item_to_string(data->shared->game_state.map[0]);  // Par exemple pour un élément spécifique
            strncpy(map_msg.messageText, map_str, sizeof(map_msg.messageText));
            
            for (int i = 0; i < data->shared->count; i++) {
                if (data->shared->fds[i] > 0) {
                    nwrite(data->shared->fds[i], &map_msg, sizeof(StructMessage));
                }
            }
        }
        printf("[BROADCAST] Message details: code=%d, text='%.10s...'\n", 
            m->code, m->messageText);
        sem_up0(data->shared->sem);
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

    // 3. NOTIFICATION DEBUT PARTIE + MAP
    msg.code = GAME_START;
    snprintf(msg.messageText, sizeof(msg.messageText), "%d", player_id);
    nwrite(client_fd, &msg, sizeof(msg));

    // Send map data
    msg.code = MAP_DATA;
    const char *map_str = item_to_string(shared->game_state.map[0]);  // Par exemple pour un élément spécifique
    strncpy(msg.messageText, map_str, sizeof(msg.messageText));
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

    // Avant sclose(client_fd);
    sem_down0(shared->sem);
    // Marquer le fd comme invalide
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (shared->fds[i] == client_fd) {
            shared->fds[i] = -1;
            break;
        }
    }
    sem_up0(shared->sem);

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
    BroadcastThreadData thread_data = {
        .pipe_read = broadcast_pipe[0],
        .shared = shared
    };

    // 2. Chargement de la map AVEC broadcast
    FileDescriptor map_fd = sopen(MAP_FILE, O_RDONLY, 0);
    load_map(map_fd, broadcast_pipe[1], &shared->game_state);
    sclose(map_fd);
    printf("Map chargée avec succès\n");

    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, broadcast_handler, &thread_data);

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
    // Avant de fermer les pipes, signaler au thread de s'arrêter
    shutdown(broadcast_pipe[1], SHUT_WR);  // Ferme proprement l'écriture
    pthread_join(broadcast_thread, NULL);
    sclose(broadcast_pipe[0]);
    sclose(broadcast_pipe[1]);
    
    return 0;
}
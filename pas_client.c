#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>

#include "utils_v3.h"
#include "messages.h"

#define IPL_CMD "./pas-cman-ipl"

int initSocketClient(char * serverIP, int serverPort) {
    int socketfd = ssocket();
    sconnect(serverIP, serverPort, socketfd);
    return socketfd;
}

int main(int argc, char *argv[]) {
    // 1. CONNEXION AU SERVEUR
    int sockfd = initSocketClient(SERVER_IP, SERVER_PORT);

    // 2. INSCRIPTION
    char pseudo[MAX_PSEUDO];
    printf("Entrez votre pseudo : ");
    fgets(pseudo, sizeof(pseudo), stdin);
    pseudo[strcspn(pseudo, "\n")] = '\0';

    StructMessage msg;
    strncpy(msg.messageText, pseudo, sizeof(msg.messageText));
    msg.code = INSCRIPTION_REQUEST;
    nwrite(sockfd, &msg, sizeof(msg));

    // 3. ATTENTE DE LA REPONSE DU SERVEUR
    if (sread(sockfd, &msg, sizeof(msg)) <= 0) {
        perror("Erreur de lecture du serveur");
        sclose(sockfd);
        exit(EXIT_FAILURE);
    }

    if (msg.code == INSCRIPTION_KO) {
        printf("Le serveur est plein. Connexion refusée.\n");
        sclose(sockfd);
        exit(EXIT_FAILURE);
    }

    // 4. ATTENTE DU DEBUT DE LA PARTIE
    printf("En attente d'un deuxième joueur...\n");
    while (1) {
        if (sread(sockfd, &msg, sizeof(msg)) <= 0) {
            perror("Erreur de lecture du serveur");
            sclose(sockfd);
            exit(EXIT_FAILURE);
        }

        if (msg.code == GAME_START) {
            printf("La partie commence !\n");
            break;
        }
    }

    // 5. CONFIGURATION DES PIPES
    // Pipe pour les commandes clavier (pas-cman-ipl -> pas_client)
    int pipe_commands[2];  
    // Pipe pour les données de jeu (pas_client -> pas-cman-ipl)
    int pipe_game_data[2]; 

    spipe(pipe_commands);
    spipe(pipe_game_data);

    // 6. LANCEMENT DE L'INTERFACE GRAPHIQUE
    pid_t ipl_pid = sfork();
    // Dans le fork de pas-cman-ipl :
    if (ipl_pid == 0) {
        // 1. Redirige STDIN pour lire depuis le socket
        sdup2(sockfd, STDIN_FILENO);
        
        // 2. Redirige STDOUT pour envoyer les commandes clavier
        sdup2(pipe_commands[1], STDOUT_FILENO);
        
        // 3. Lance l'interface graphique
        sexecl(IPL_CMD, IPL_CMD, NULL);
    }

    // 7. BOUCLE DE COMMUNICATION
    struct pollfd fds[2] = {
        {sockfd, POLLIN, 0},          // Surveille le socket pour les données du serveur
        {pipe_commands[0], POLLIN, 0}  // Surveille le pipe pour les commandes clavier
    };

    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret == -1) {
            perror("poll");
            break;
        }

        // Données reçues du serveur
        if (fds[0].revents & POLLIN) {
            char buffer[1024];
            ssize_t n = read(sockfd, buffer, sizeof(buffer));
            if (n <= 0) break;
            
            // Transfère au pas-cman-ipl via pipe_game_data
            nwrite(pipe_game_data[1], buffer, n);
        }

        // Commandes clavier reçues de pas-cman-ipl
        if (fds[1].revents & POLLIN) {
            char command[256];
            ssize_t n = read(pipe_commands[0], command, sizeof(command));
            if (n <= 0) break;
            
            // Transfère au serveur via le socket
            nwrite(sockfd, command, n);
        }
    }

    // 8. NETTOYAGE
    skill(ipl_pid, SIGTERM);
    swaitpid(ipl_pid, NULL, 0);
    sclose(sockfd);
    sclose(pipe_commands[0]);
    sclose(pipe_game_data[1]);

    return 0;
}
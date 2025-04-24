#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "utils_v3.h"
#include "messages.h"

#define IPL_CMD "./pas-cman-ipl"

int initSocketClient(char * serverIP, int serverPort) {
    int socketfd = ssocket();
    sconnect(serverIP, serverPort, socketfd);
    return socketfd;
}

int main(int argc, char *argv[]) {
    // 1. Connexion au serveur
    int sockfd = initSocketClient(SERVER_IP, SERVER_PORT);

    // 2. Inscription
    char pseudo[MAX_PSEUDO];
    printf("Entrez votre pseudo : ");
    fgets(pseudo, sizeof(pseudo), stdin);
    pseudo[strcspn(pseudo, "\n")] = '\0';

    StructMessage msg;
    strncpy(msg.messageText, pseudo, sizeof(msg.messageText));
    msg.code = INSCRIPTION_REQUEST;
    nwrite(sockfd, &msg, sizeof(msg));

    // 3. Attente de la réponse
    if (sread(sockfd, &msg, sizeof(msg)) <= 0) {
        perror("Erreur lecture serveur");
        sclose(sockfd);
        exit(EXIT_FAILURE);
    }

    if (msg.code == INSCRIPTION_KO) {
        printf("Connexion refusée : serveur plein.\n");
        sclose(sockfd);
        exit(EXIT_FAILURE);
    }

    // 4. Attente de GAME_START
    printf("En attente d’un deuxième joueur...\n");

    while (1) {
        if (sread(sockfd, &msg, sizeof(msg)) <= 0) {
            perror("Erreur lecture serveur");
            sclose(sockfd);
            exit(EXIT_FAILURE);
        }

        if (msg.code == GAME_START) {
            printf("La partie commence !\n");

            // 5. Lecture de la MAP
            if (sread(sockfd, &msg, sizeof(msg)) <= 0 || msg.code != MAP_DATA) {
                perror("Erreur lecture MAP");
                sclose(sockfd);
                exit(EXIT_FAILURE);
            }

            // 6. Création d’un pipe
            int pipe_commands[2];
            spipe(pipe_commands);

            // 7. Fork pour interface graphique
            pid_t ipl_pid = sfork();
            if (ipl_pid == 0) {
                // Fils : Interface
                sclose(pipe_commands[1]);                  // Ferme écriture
                sdup2(pipe_commands[0], STDIN_FILENO);     // Lit depuis le pipe
                sclose(pipe_commands[0]);                  // Ferme original
                sexecl(IPL_CMD, IPL_CMD, NULL);
                exit(EXIT_FAILURE);
            }

            // Père : Écrit la MAP
            sclose(pipe_commands[0]); // Ferme lecture
            nwrite(pipe_commands[1], msg.messageText, strlen(msg.messageText));

            // 8. Boucle communication (socket <-> interface graphique)
            struct pollfd fds[2] = {
                {sockfd, POLLIN, 0},
                {pipe_commands[1], POLLIN, 0}
            };

            while (1) {
                int res = poll(fds, 2, -1);
                if (res == -1) {
                    perror("poll");
                    break;
                }

                if (fds[0].revents & POLLIN) {
                    char buffer[1024];
                    ssize_t n = sread(sockfd, buffer, sizeof(buffer));
                    if (n <= 0) break;
                    // Traitement éventuel ici
                }

                if (fds[1].revents & POLLIN) {
                    char cmd[256];
                    ssize_t n = sread(pipe_commands[1], cmd, sizeof(cmd));
                    if (n <= 0) break;
                    nwrite(sockfd, cmd, n);
                }
            }

            // 9. Nettoyage
            skill(ipl_pid, SIGTERM);
            swaitpid(ipl_pid, NULL, 0);
            sclose(pipe_commands[1]);
            break;
        }
    }

    sclose(sockfd);
    return 0;
}

#ifndef _MESSAGES_H_
#define _MESSAGES_H_

#define SERVER_PORT 15785
#define SERVER_IP "127.0.0.1" /* localhost */
#define MAX_PSEUDO 256
#define MAX_PLAYERS 2
#define WAIT_TIME 30  // Temps d'attente en secondes pour un deuxième joueur

typedef enum
{
  INSCRIPTION_REQUEST = 10,
  INSCRIPTION_OK = 11,
  INSCRIPTION_KO = 12,
  GAME_START = 20,      // La partie peut commencer (2 joueurs connectés)
  GAME_CANCEL = 21,     // Pas assez de joueurs après le temps d'attente
  PLAYER_MOVE = 30,     // Un joueur a fait un mouvement (pour extension future)
  GAME_END = 40,         // Fin de partie (pour extension future)
  MAP_DATA = 50
} Code;

/* struct message used between server and client */
typedef struct
{
  char messageText[MAX_PSEUDO];
  int code;
  int player_id;
} StructMessage;

#endif
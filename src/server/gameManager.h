#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include "common/protocol.h"

typedef struct {
    unsigned int game_id; // ID della partita
    char *game_name; // Nome della partita
    int game_pipe_fd; // File descriptor della pipe per comunicare con il thread del gioco (per ricevere nuovi giocatori)
} GameThreadArg;

void *game_thread(void *arg);

void cleanup_client_game(int epoll_fd, int client_fd, unsigned int player_id);

int send_player_info(int client_fd, unsigned int player_id, char *username);
int handle_player_action(unsigned int player_id, PayloadNode *payload);

#endif // GAME_MANAGER_H
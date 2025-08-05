#ifndef CLIENT_GAME_MANAGER_H
#define CLIENT_GAME_MANAGER_H

#include <pthread.h>
#include "common/protocol.h"
#include "common/game.h"

extern UserInfo *user;

extern GameState *game;
extern pthread_mutex_t game_state_mutex;

extern FILE *client_log_file;


void handle_game_msg(int conn_s, unsigned int game_id, char *game_name);
int handle_player_action(int player_id, Payload *payload);
void handle_generic_msg(uint16_t msg_type, Payload *payload);

void print_log_file();

#endif // CLIENT_GAME_MANAGER_H
#ifndef CLIENT_GAME_MANAGER_H
#define CLIENT_GAME_MANAGER_H

#include "common/protocol.h"

void handle_game_msg(int conn_s, unsigned int game_id, char *game_name);
int handle_player_action(int player_id, Payload *payload);
void handle_generic_msg(uint16_t msg_type, Payload *payload);

void print_log_file();

#endif // CLIENT_GAME_MANAGER_H
#ifndef CLIENT_GAME_MANAGER_H
#define CLIENT_GAME_MANAGER_H

#include <pthread.h>
#include "common/protocol.h"
#include "common/game.h"

extern UserInfo *user;
extern int is_owner;
extern int local_player_turn_index;

extern GameState *game;
extern pthread_mutex_t game_state_mutex;

extern FILE *client_log_file;


void handle_game_msg(int conn_s, unsigned int game_id, char *game_name);

void on_game_state_update_msg(Payload *payload);
void on_player_joined_msg(Payload *payload);
void on_player_left_msg(Payload *payload);
void on_fleet_setup_reminder_msg();
void on_game_started_msg(Payload *payload);
void on_turn_order_update_msg(Payload *payload);
void on_your_turn_msg();
void on_attack_update_msg(Payload *payload);
void on_you_are_eliminated_msg();
void on_game_finished_msg(Payload *payload);

void handle_generic_msg(uint16_t msg_type);

void print_log_file();

#endif // CLIENT_GAME_MANAGER_H
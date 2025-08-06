#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include "common/protocol.h"
#include "common/game.h"

typedef struct {
    unsigned int game_id; // ID della partita
    char *game_name; // Nome della partita
    int game_pipe_fd; // File descriptor della pipe per comunicare con il thread del gioco (per ricevere nuovi giocatori)
} GameThreadArg;

typedef struct{
    time_t start_time; // Tempo di inizio del timer
    int duration; // Durata del timer in secondi
} TimerInfo;

typedef enum {
    GAME_WAITING_FOR_PLAYERS,
    GAME_WAITING_FLEET_SETUP,
    GAME_IN_PROGRESS,
    GAME_FINISHED
} GameStateType;

void *game_thread(void *arg);

void cleanup_client_game(int epoll_fd, int client_fd, unsigned int player_id);

int send_player_info(int client_fd, unsigned int player_id, char *username);
int send_to_all_players(GameState *game, uint16_t msg_type, Payload *payload);

void set_epoll_timer(TimerInfo *timer_info, int duration);
void update_epoll_timer(TimerInfo *timer_info);

#endif // GAME_MANAGER_H
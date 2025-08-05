#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/epoll.h>
#include <errno.h>

#include "common/protocol.h"
#include "common/game.h"
#include "utils/debug.h"
#include "server/users.h"
#include "server/gameManager.h"
#include "server/lobbyManager.h"

#define LOG_TAG current_game->game_name
#define MAX_EVENTS 128

// Game corrente per il thread, usato per evitare conflitti tra più thread
// Non è thread-safe, ogni thread deve usare la propria copia
__thread GameState *current_game = NULL;

/**
 * Thread di gioco che gestisce le interazioni tra i giocatori in una partita.
 * Si occupa di accettare nuovi giocatori, gestire i messaggi e le azioni di gioco.
 */
void *game_thread(void *arg) {
    GameThreadArg *game_arg = (GameThreadArg *)arg;
    int game_pipe_fd = game_arg->game_pipe_fd;
    int game_epoll_fd = epoll_create1(0);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = UINT64_MAX; // Indica che è un evento di connessione
    epoll_ctl(game_epoll_fd, EPOLL_CTL_ADD, game_pipe_fd, &ev);

    current_game = create_game_state(game_arg->game_id, game_arg->game_name);
    free(game_arg->game_name);
    free(game_arg);

    while (1) {
        struct epoll_event events[MAX_EVENTS];
        // TODO utilizzare il parametro timeout di epoll_wait per gestire un timer
        int nfds = epoll_wait(game_epoll_fd, events, MAX_EVENTS, -1);

        for(int n = 0; n < nfds; n++){
            if(events[n].data.u64 == UINT64_MAX) {
                int new_player_id;
                if (read(game_pipe_fd, &new_player_id, sizeof(new_player_id)) == -1) {
                    LOG_ERROR_TAG("Errore durante la lettura dalla pipe del nuovo giocatore");
                    continue; // Continua ad accettare altre connessioni
                }

                int conn_s = get_user_socket_fd(new_player_id);
                if(conn_s < 0) {
                    LOG_WARNING_TAG("Errore nell'ottenimento della socket per il giocatore %d", new_player_id);
                    continue; // Continua ad accettare altre connessioni
                }

                ev.events = EPOLLIN;
                ev.data.u64 = new_player_id;
                epoll_ctl(game_epoll_fd, EPOLL_CTL_ADD, conn_s, &ev);

                LOG_INFO_TAG("Nuovo giocatore connesso: %d", new_player_id);
                // Aggiungi il giocatore allo stato del gioco
                char *username = get_username_by_id(new_player_id);
                if (username == NULL) {
                    LOG_ERROR_TAG("Errore durante l'ottenimento del nome utente per il giocatore %d", new_player_id);
                    continue; // Continua ad accettare altri giocatori
                }
                if (add_player_to_game_state(current_game, new_player_id, username) < 0) {
                    LOG_ERROR_TAG("Errore durante l'aggiunta del giocatore %d:`%s` alla partita", new_player_id, username);
                    free(username);
                    continue; // Continua ad accettare altri giocatori
                }


                free(username);

            }else{
                unsigned int player_id = events[n].data.u64;
                int client_s = get_user_socket_fd(player_id);
                if (client_s < 0) {
                    // Se non riusciamo a ottenere il file descriptor della socket, salta questo evento
                    // e.g., il giocatore potrebbe essersi disconnesso
                    LOG_WARNING_TAG("Errore nell'ottenimento della socket per il giocatore %d", player_id);
                    continue; // Continua ad accettare altri messaggi
                }

                uint16_t msg_type;
                Payload *payload = NULL;
                if(safeRecvMsg(client_s, &msg_type, &payload) < 0){
                    LOG_MSG_ERROR_TAG("Errore durante la ricezione del messaggio dal player %d, procedo a chiuderne la connessione...", player_id);
                    cleanup_client_game(game_epoll_fd, client_s, player_id);
                    continue; // Continua ad accettare altri messaggi
                }

                switch(msg_type){
                    case MSG_READY_TO_PLAY:
                        LOG_DEBUG_TAG("Il giocatore %d è pronto a giocare", player_id);
                        // Invia le informazioni sui giocatori già presenti nella partita al nuovo giocatore
                        Payload *gameStatePayload = createEmptyPayload();
                        addPayloadKeyValuePair(gameStatePayload, "type", "game_info");
                        addPayloadKeyValuePairInt(gameStatePayload, "game_id", current_game->game_id);
                        addPayloadKeyValuePair(gameStatePayload, "game_name", current_game->game_name);

                        for(unsigned int i = 0; i < current_game->players_count; i++) {
                            if(current_game->players[i].user.user_id == player_id) {
                                // Non inviare le informazioni del giocatore che si sta unendo
                                continue;
                            }
                            addPayloadList(gameStatePayload);
                            addPayloadKeyValuePair(gameStatePayload, "type", "player_info");

                            addPayloadKeyValuePairInt(gameStatePayload, "player_id", current_game->players[i].user.user_id);
                            addPayloadKeyValuePair(gameStatePayload, "username", current_game->players[i].user.username);

                            // Invia le informazioni del nuovo giocatore a tutti gli altri giocatori
                            int client_s = get_user_socket_fd(current_game->players[i].user.user_id);
                            if (send_player_info(client_s, player_id, current_game->players[player_id].user.username) < 0) {
                                LOG_MSG_ERROR_TAG("Errore durante l'invio delle informazioni del nuovo giocatore %d", player_id);
                            }
                        }

                        if (safeSendMsg(client_s, MSG_GAME_STATE_UPDATE, gameStatePayload) < 0) {
                            LOG_MSG_ERROR_TAG("Errore durante l'invio dello stato del gioco al giocatore %d", player_id);
                            cleanup_client_game(game_epoll_fd, client_s, player_id);
                        }

                        break;

                    case MSG_START_GAME:
                        if ((int)player_id == get_game_owner_id(current_game->game_id)) {
                            // Inizia il gioco
                            LOG_INFO_TAG("Il giocatore %d ha iniziato il gioco.", player_id);
                            for(unsigned int i = 0; i < current_game->players_count; i++){
                                unsigned int player_id = current_game->players[i].user.user_id;
                                int client_s = get_user_socket_fd(player_id);
                                // Invia il messaggio di inizio partita a tutti i giocatori
                                safeSendMsg(client_s, MSG_GAME_STARTED, NULL);
                            }
                        } else {
                            LOG_ERROR_TAG("Il giocatore %d ha tentato di avviare la partita, ma non ne è il proprietario", player_id);
                            safeSendMsg(client_s, MSG_ERROR_NOT_YOUR_TURN, NULL);
                        }
                        break;

                    case MSG_GAME_ACTION:
                        if (current_game->players[current_game->player_turn].user.user_id == player_id) {
                            // Gestisci l'azione del giocatore
                            handle_player_action(player_id, payload);
                            LOG_DEBUG_TAG("Il giocatore %d ha eseguito un'azione", player_id);
                            current_game->player_turn = (current_game->player_turn + 1) % current_game->players_count;
                            safeSendMsg(get_user_socket_fd(current_game->players[current_game->player_turn].user.user_id), MSG_YOUR_TURN, NULL);
                        } else {
                            LOG_WARNING_TAG("Il giocatore %d ha provato a eseguire un'azione, ma non è il suo turno", player_id);
                            safeSendMsg(client_s, MSG_ERROR_NOT_YOUR_TURN, NULL);
                        }
                        break;
                        
                    default:
                        on_unexpected_msg(game_epoll_fd, player_id, client_s, msg_type);
                        break;
                }

                freePayload(payload);
                
            }
        }
    }

    return NULL;
}


/**
 * Invia le informazioni del giocatore al client.
 * @param client_fd File descriptor della socket del client.
 * @param player_id ID del giocatore.
 * @param username Nome utente del giocatore.
 * @return 0 se l'invio è andato a buon fine, -1 in caso di errore.
 */
int send_player_info(int client_fd, unsigned int player_id, char *username) {
    char player_id_str[32];
    snprintf(player_id_str, sizeof(player_id_str), "%u", player_id);

    Payload *payload = createEmptyPayload();
    addPayloadKeyValuePair(payload, "player_id", player_id_str);
    addPayloadKeyValuePair(payload, "username", username);

    int result = safeSendMsg(client_fd, MSG_PLAYER_JOINED, payload);
    return result;
}


/**
 * Gestisce un'azione del giocatore.
 * @param player_id ID del giocatore che ha eseguito l'azione.
 * @param payload Payload contenente i dettagli dell'azione.
 * @return 0 se l'azione è stata gestita correttamente, -1 in caso di errore.
 */
int handle_player_action(unsigned int player_id, Payload *payload) {
    char *action = getPayloadValue(payload, 0, "action");
    if (action == NULL) {
        LOG_ERROR_TAG("Azione non specificata per il giocatore %d", player_id);
        return -1;
    }

    // if(strcmp(action, "attack") == 0) {
    //     // Gestisci l'attacco del giocatore
    //     int x = atoi(getPayloadValue(payload, 0, "x"));
    //     int y = atoi(getPayloadValue(payload, 0, "y"));
    //     int adversary_id = atoi(getPayloadValue(payload, 0, "adversary_id"));
    //     PlayerState *player_state = get_player_state(current_game, player_id);
    //     PlayerState *adversary_state = get_player_state(current_game, adversary_id);
    //     if (player_state == NULL || adversary_state == NULL) {
    //         LOG_ERROR("Giocatore %d o avversario %d non trovato nella partita.\n", player_id, adversary_id);
    //         free(action);
    //         return -1;
    //     }

    //     if (attack(&adversary_state->board, x, y) < 0) {
    //         LOG_ERROR_TAG("Errore durante l'attacco del giocatore %d alla posizione (%d, %d)", adversary_id, x, y);
    //         free(action);
    //         return -1;
    //     }
    // } else {
    //     LOG_ERROR_TAG("Azione '%s' non riconosciuta per il giocatore %d", action, player_id);
    //     free(action);
    //     return -1;
    // }

    free(action);
    return 0;
}

/**
 * Pulisce le risorse associate a un client disconnesso in una partita.
 * Rimuove il client dall'epoll e dallo stato del gioco, e gestisce eventuali cleanup necessari.
 * @param epoll_fd File descriptor dell'epoll.
 * @param client_fd File descriptor della socket del client.
 * @param player_id ID del giocatore da rimuovere.
 */
void cleanup_client_game(int epoll_fd, int client_fd, unsigned int player_id) {
    // Rimuovi il client dall'epoll
    cleanup_client_lobby(epoll_fd, client_fd, player_id);
    remove_player_from_game_state(current_game, player_id);
    // TODO dovrei evitare di rimuovere il giocatore per permettere la riconnessione
}
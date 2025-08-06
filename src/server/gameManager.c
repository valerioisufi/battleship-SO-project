#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/epoll.h>
#include <errno.h>
#include <time.h>

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
__thread GameStateType current_game_state_type = GAME_WAITING_FOR_PLAYERS;
__thread TimerInfo timer_info = {0, -1};

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
        update_epoll_timer(&timer_info);
        int nfds = epoll_wait(game_epoll_fd, events, MAX_EVENTS, timer_info.duration * 1000);
        if (nfds == 0){
            // Timeout scaduto, gestisci il timeout
            if (timer_info.duration > 0) {
                if(current_game_state_type == GAME_WAITING_FLEET_SETUP) {
                    LOG_WARNING_TAG("Il tempo per piazzare le navi è scaduto, la partita inizierà senza di esse");
                    // Invia un messaggio a tutti i giocatori che il tempo è scaduto
                    for (unsigned int i = 0; i < current_game->players_count; i++) {
                        if(current_game->players[i].fleet == NULL) {
                            int client_s = get_user_socket_fd(current_game->players[i].user.user_id);
                            if (client_s < 0) {
                                LOG_WARNING_TAG("Errore nell'ottenimento della socket per il giocatore %d", current_game->players[i].user.user_id);
                                continue; // Continua ad accettare altri giocatori
                            }
                            cleanup_client_game(game_epoll_fd, client_s, current_game->players[i].user.user_id);
                        }
                    }
                    current_game_state_type = GAME_IN_PROGRESS;

                } else if(current_game_state_type == GAME_IN_PROGRESS) {
                    LOG_WARNING_TAG("Il tempo per il turno è scaduto, il turno passerà al prossimo giocatore");
                    // Passa al turno successivo
                    current_game->player_turn = (current_game->player_turn + 1) % current_game->players_count;
                    safeSendMsg(get_user_socket_fd(current_game->players[current_game->player_turn].user.user_id), MSG_YOUR_TURN, NULL);
                    set_epoll_timer(&timer_info, 60); // Resetta il timer a 60 secondi per il prossimo turno
                }
            }
            continue;
        } else if (nfds < 0) {
            LOG_ERROR_TAG("Errore durante l'attesa di eventi sull'epoll: %s", strerror(errno));
            break; // Esci dal loop in caso di errore
        }

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

                if(current_game_state_type != GAME_WAITING_FOR_PLAYERS) {
                    LOG_WARNING_TAG("Nuovo giocatore con ID %d si è connesso, ma la partita non è in attesa di giocatori", new_player_id);
                    LOG_DEBUG_TAG("Stato attuale della partita: %d", current_game_state_type);
                    continue; // Continua ad accettare altri giocatori
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

                    case MSG_SETUP_FLEET:
                        LOG_DEBUG_TAG("Il giocatore %d ha inviato la configurazione della flotta", player_id);
                        if (current_game->players[player_id].fleet != NULL) {
                            LOG_WARNING_TAG("Il giocatore %d ha già inviato la configurazione della flotta, ignorando il nuovo messaggio", player_id);
                            continue;
                        }
                        current_game->players[player_id].fleet = malloc(sizeof(FleetSetup));
                        if (current_game->players[player_id].fleet == NULL) {
                            LOG_ERROR_TAG("Errore durante l'allocazione della flotta per il giocatore %d", player_id);
                            continue;
                        }
                        memset(current_game->players[player_id].fleet, 0, sizeof(FleetSetup));

                        int is_fleet_valid = 1;
                        for(int i = 0; i < getPayloadListSize(payload); i++) {
                            int dim, vertical, x, y;
                            if(getPayloadIntValue(payload, i, "dim", &dim) ||
                               getPayloadIntValue(payload, i, "vertical", &vertical) ||
                               getPayloadIntValue(payload, i, "x", &x) ||
                               getPayloadIntValue(payload, i, "y", &y)) {
                                LOG_ERROR_TAG("Errore durante l'ottenimento dei dati della nave dal payload");
                                continue; // Continua con il prossimo elemento del payload
                            }

                            current_game->players[player_id].fleet->ships[i].dim = dim;
                            current_game->players[player_id].fleet->ships[i].vertical = vertical;
                            current_game->players[player_id].fleet->ships[i].x = x;
                            current_game->players[player_id].fleet->ships[i].y = y;

                            if(place_ship(&current_game->players[player_id].board, &current_game->players[player_id].fleet->ships[i])){
                                LOG_ERROR_TAG("Errore durante il piazzamento della nave %d per il giocatore %d", i, player_id);
                                is_fleet_valid = 0;
                                break; // Esci dal ciclo se il piazzamento fallisce
                            }
                        }

                        if (current_game->players[player_id].board.ships_left != NUM_SHIPS) {
                            LOG_WARNING_TAG("Il giocatore %d ha inviato una flotta incompleta, ignorando la richiesta", player_id);
                            is_fleet_valid = 0;
                        }

                        int dim5 = 0, dim4 = 0, dim3 = 0, dim2 = 0;
                        for (int i = 0; i < NUM_SHIPS; i++) {
                            int dim = current_game->players[player_id].fleet->ships[i].dim;
                            if (dim == 5) dim5++;
                            else if (dim == 4) dim4++;
                            else if (dim == 3) dim3++;
                            else if (dim == 2) dim2++;
                        }

                        if (dim5 != fleet_requirement.dim5 || dim4 != fleet_requirement.dim4 || dim3 != fleet_requirement.dim3 || dim2 != fleet_requirement.dim2) {
                            LOG_WARNING_TAG("Il giocatore %d ha inviato una flotta con dimensioni non valide: 5=%d, 4=%d, 3=%d, 2=%d", 
                                            player_id, dim5, dim4, dim3, dim2);
                            is_fleet_valid = 0;
                        }


                        if(is_fleet_valid){
                            LOG_INFO_TAG("La flotta del giocatore %d è stata piazzata correttamente", player_id);
                            if(current_game_state_type == GAME_WAITING_FLEET_SETUP){
                                unsigned int count_ready_players = 0;
                                for(unsigned int i = 0; i < current_game->players_count; i++) {
                                    if(current_game->players[i].fleet == NULL){
                                        break;
                                    }
                                    count_ready_players++;
                                }
                                if(count_ready_players == current_game->players_count) {
                                    LOG_INFO_TAG("Tutti i giocatori hanno piazzato le navi, il gioco può iniziare");
                                    current_game->player_turn = (current_game->player_turn + 1) % current_game->players_count;
                                    safeSendMsg(get_user_socket_fd(current_game->players[current_game->player_turn].user.user_id), MSG_YOUR_TURN, NULL);
                                    set_epoll_timer(&timer_info, 60); // Resetta il timer a 60 secondi per il prossimo turno

                                }
                            }
                        } else {
                            LOG_WARNING_TAG("La flotta del giocatore %d non è valida", player_id);
                            init_board(&current_game->players[player_id].board);
                            free(current_game->players[player_id].fleet);
                            current_game->players[player_id].fleet = NULL;
                        }

                        break;

                    case MSG_START_GAME:
                        if ((int)player_id == get_game_owner_id(current_game->game_id)) {
                            if(current_game_state_type != GAME_WAITING_FOR_PLAYERS){
                                LOG_WARNING_TAG("Il giocatore %d ha tentato di avviare il gioco, ma non è in attesa di giocatori", player_id);
                                continue; // Il gioco può essere avviato solo quando è in attesa di giocatori
                            }
                            // Inizia il gioco
                            LOG_INFO_TAG("Il giocatore %d ha iniziato il gioco.", player_id);

                            set_game_started(current_game->game_id, 1);

                            send_to_all_players(current_game, MSG_GAME_STARTED, NULL);
                            unsigned int count_ready_players = 0;
                            for(unsigned int i = 0; i < current_game->players_count; i++) {
                                if(current_game->players[i].fleet == NULL){
                                    break;
                                }
                                count_ready_players++;
                            }
                            if(count_ready_players == current_game->players_count) {
                                LOG_INFO_TAG("Tutti i giocatori hanno piazzato le navi, il gioco può iniziare");
                                current_game_state_type = GAME_IN_PROGRESS;
                                current_game->player_turn = (current_game->player_turn + 1) % current_game->players_count;
                                safeSendMsg(get_user_socket_fd(current_game->players[current_game->player_turn].user.user_id), MSG_YOUR_TURN, NULL);
                                set_epoll_timer(&timer_info, 60); // Resetta il timer a 60 secondi per il prossimo turno
                            } else {
                                LOG_WARNING_TAG("Non tutti i giocatori hanno piazzato le navi, il gioco non può iniziare");
                                current_game_state_type = GAME_WAITING_FLEET_SETUP;
                                set_epoll_timer(&timer_info, 120); // Imposta il timer a 120 secondi per consentire a chi ancora non ha piazzato le navi di farlo
                            }
                        } else {
                            LOG_ERROR_TAG("Il giocatore %d ha tentato di avviare la partita, ma non ne è il proprietario", player_id);
                            safeSendMsg(client_s, MSG_ERROR_NOT_YOUR_TURN, NULL);
                        }
                        break;

                    case MSG_ATTACK:
                        if (current_game_state_type != GAME_IN_PROGRESS) {
                            LOG_WARNING_TAG("Il giocatore %d ha tentato di attaccare, ma il gioco non è in corso", player_id);
                            safeSendMsg(client_s, MSG_ERROR_PLAYER_ACTION, NULL);
                            continue; // Continua ad accettare altri messaggi
                        }
                        if (current_game->players[current_game->player_turn].user.user_id == player_id) {
                            // Gestisci l'azione del giocatore
                            LOG_DEBUG_TAG("Il giocatore %d ha eseguito un attacco", player_id);
                            int player_id, x, y;
                            if(getPayloadIntValue(payload, 0, "player_id", &player_id) || getPayloadIntValue(payload, 0, "x", &x) || getPayloadIntValue(payload, 0, "y", &y)) {
                                LOG_ERROR_TAG("Errore durante l'ottenimento delle coordinate dell'attacco dal payload");
                                safeSendMsg(client_s, MSG_ERROR_MALFORMED_MESSAGE, NULL);
                                continue; // Continua ad accettare altri messaggi
                            }
                            PlayerState *attacked_player = get_player_state(current_game, player_id);
                            if (attacked_player == NULL) {
                                LOG_ERROR_TAG("Il giocatore %d non esiste nella partita", player_id);
                                safeSendMsg(client_s, MSG_ERROR_PLAYER_ACTION, NULL);
                                continue; // Continua ad accettare altri messaggi
                            }

                            int ret = attack(attacked_player, x, y);
                            if (ret < 0) {
                                LOG_ERROR_TAG("Errore durante l'attacco del giocatore %d alla posizione (%d, %d)", player_id, x, y);
                                safeSendMsg(client_s, MSG_ERROR_PLAYER_ACTION, NULL);
                                continue; // Continua ad accettare altri messaggi
                            }
                            Payload *attack_payload = createEmptyPayload();
                            addPayloadKeyValuePairInt(attack_payload, "attacker_id", player_id);
                            addPayloadKeyValuePairInt(attack_payload, "attacked_id", attacked_player->user.user_id);
                            addPayloadKeyValuePairInt(attack_payload, "x", x);
                            addPayloadKeyValuePairInt(attack_payload, "y", y);
                            if(ret == 0) {
                                LOG_INFO_TAG("Il giocatore %d ha mancato l'attacco alla posizione (%d, %d)", player_id, x, y);
                                addPayloadKeyValuePair(attack_payload, "result", "miss");
                            } else if(ret == 1) {
                                LOG_INFO_TAG("Il giocatore %d ha colpito una nave alla posizione (%d, %d)", player_id, x, y);
                                addPayloadKeyValuePair(attack_payload, "result", "hit");
                            } else if(ret == 2) {
                                LOG_INFO_TAG("Il giocatore %d ha affondato una nave alla posizione (%d, %d)", player_id, x, y);
                                addPayloadKeyValuePair(attack_payload, "result", "sunk");
                            }

                            send_to_all_players(current_game, MSG_ATTACK_UPDATE, attack_payload);

                            current_game->player_turn = (current_game->player_turn + 1) % current_game->players_count;
                            safeSendMsg(get_user_socket_fd(current_game->players[current_game->player_turn].user.user_id), MSG_YOUR_TURN, NULL);
                            set_epoll_timer(&timer_info, 60); // Resetta il timer a 60 secondi per il prossimo turno
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

int send_to_all_players(GameState *game, uint16_t msg_type, Payload *payload) {
    for (unsigned int i = 0; i < game->players_count; i++) {
        int client_fd = get_user_socket_fd(game->players[i].user.user_id);
        if (client_fd < 0) {
            LOG_WARNING_TAG("Impossibile ottenere il file descriptor per il giocatore %d", game->players[i].user.user_id);
            continue; // Continua ad inviare agli altri giocatori
        }
        if (safeSendMsgWithoutCleanup(client_fd, msg_type, payload) < 0) {
            LOG_ERROR_TAG("Errore durante l'invio del messaggio %d al giocatore %d", msg_type, game->players[i].user.user_id);
        }
    }
    freePayload(payload);
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

    Payload *payload = createEmptyPayload();
    addPayloadKeyValuePairInt(payload, "player_id", player_id);
    send_to_all_players(current_game, MSG_PLAYER_LEFT, payload);
    // TODO dovrei evitare di rimuovere il giocatore per permettere la riconnessione
}


void set_epoll_timer(TimerInfo *timer_info, int duration){
    time_t current_time = time(NULL);
    timer_info->start_time = current_time;
    timer_info->duration = duration;
}

void update_epoll_timer(TimerInfo *timer_info) {
    if (timer_info->duration <= 0) {
        return; // Nessun timer attivo
    }

    time_t current_time = time(NULL);
    if (current_time - timer_info->start_time >= timer_info->duration) {
        // Il timer è scaduto, esegui l'azione desiderata
        LOG_INFO("Il timer è scaduto dopo %d secondi", timer_info->duration);
        // rimuovo il timer
        timer_info->start_time = 0;
        timer_info->duration = -1;
    } else {
        timer_info->duration -= (current_time - timer_info->start_time);
        timer_info->start_time = current_time;
    }
}
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
        int nfds = epoll_wait(game_epoll_fd, events, MAX_EVENTS, get_epoll_timer(&timer_info) * 1000);
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

                    update_turn_order(current_game, game_epoll_fd);
                } else if(current_game_state_type == GAME_IN_PROGRESS) {
                    LOG_WARNING_TAG("Il tempo per il turno è scaduto, il turno passerà al prossimo giocatore");
                    // Passa al turno successivo
                    update_turn_order(current_game, game_epoll_fd);
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
                    cleanup_client_game(game_epoll_fd, conn_s, new_player_id);
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
                        on_ready_to_play_msg(game_epoll_fd, client_s, player_id);
                        break;

                    case MSG_SETUP_FLEET:
                        on_setup_fleet_msg(game_epoll_fd, client_s, player_id, payload);
                        break;

                    case MSG_START_GAME:
                        on_start_game_msg(game_epoll_fd, client_s, player_id);
                        break;

                    case MSG_ATTACK:
                        on_attack_msg(game_epoll_fd, client_s, player_id, payload);
                        break;
                        
                    default:
                        on_unexpected_game_msg(game_epoll_fd, client_s, player_id, msg_type);
                        break;
                }

                freePayload(payload);
                
            }
        }
    }

    return NULL;
}

/**
 * Gestisce il messaggio di un giocatore che è pronto a giocare.
 * Invia le informazioni sui giocatori già presenti nella partita al nuovo giocatore.
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 * @param client_s File descriptor della socket del client.
 * @param player_id ID del giocatore che è pronto a giocare.
 */
void on_ready_to_play_msg(int game_epoll_fd, int client_s, unsigned int player_id) {
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
        if (current_game->players[i].user.username != NULL) {
            addPayloadKeyValuePair(gameStatePayload, "username", current_game->players[i].user.username);
        } else {
            LOG_DEBUG("Username not found for player %d, using fallback", current_game->players[i].user.user_id);
            addPayloadKeyValuePair(gameStatePayload, "username", "Unknown"); // Fallback
        }
    }

    if (safeSendMsg(client_s, MSG_GAME_STATE_UPDATE, gameStatePayload) < 0) {
        LOG_MSG_ERROR_TAG("Errore durante l'invio dello stato del gioco al giocatore %d", player_id);
        cleanup_client_game(game_epoll_fd, client_s, player_id);
        return;
    }

    Payload *payload = createEmptyPayload();
    addPayloadKeyValuePairInt(payload, "player_id", player_id);
    addPayloadKeyValuePair(payload, "username", current_game->players[player_id].user.username);
    send_to_all_players(current_game, MSG_PLAYER_JOINED, payload, player_id);
}

/**
 * Gestisce il messaggio di configurazione della flotta da parte di un giocatore.
 * Verifica la validità della flotta e aggiorna lo stato del gioco.
 * Se la flotta è valida, invia un messaggio a tutti i giocatori che il giocatore ha piazzato le navi.
 * Se la flotta non è valida, resetta la griglia del giocatore e libera le risorse della flotta.
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 * @param client_s File descriptor della socket del client.
 * @param player_id ID del giocatore che sta configurando la flotta.
 * @param payload Payload del messaggio ricevuto contenente le informazioni sulla flotta.
 */
void on_setup_fleet_msg(int game_epoll_fd, int client_s, unsigned int player_id, Payload *payload) {
    LOG_DEBUG_TAG("Il giocatore %d ha inviato la configurazione della flotta", player_id);

    if (current_game_state_type != GAME_WAITING_FOR_PLAYERS && current_game_state_type != GAME_WAITING_FLEET_SETUP) {
        LOG_WARNING_TAG("Il giocatore %d ha inviato la configurazione della flotta, ma il gioco non è in attesa di piazzamento navi", player_id);
        on_unexpected_game_msg(game_epoll_fd, client_s, player_id, MSG_SETUP_FLEET);
        return; // Il gioco non è in attesa di piazzamento navi
    }

    PlayerState *player_state = get_player_state(current_game, player_id);
    if (player_state == NULL) {
        LOG_ERROR_TAG("Stato del giocatore non trovato per l'ID %d", player_id);
        return;
    }

    if (player_state->fleet != NULL) {
        LOG_WARNING_TAG("Il giocatore %d ha già inviato la configurazione della flotta, ignorando il nuovo messaggio", player_id);
        on_unexpected_game_msg(game_epoll_fd, client_s, player_id, MSG_SETUP_FLEET);
        return;
    }
    player_state->fleet = malloc(sizeof(FleetSetup));
    if (player_state->fleet == NULL) {
        LOG_ERROR_TAG("Errore durante l'allocazione della flotta per il giocatore %d", player_id);
        return;
    }
    memset(player_state->fleet, 0, sizeof(FleetSetup));
    // init_board(&player_state->board);

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

        player_state->fleet->ships[i].dim = dim;
        player_state->fleet->ships[i].vertical = vertical;
        player_state->fleet->ships[i].x = x;
        player_state->fleet->ships[i].y = y;

        LOG_DEBUG_TAG("Nave %d per il giocatore %d: dim=%d, vertical=%d, x=%d, y=%d", i, player_id, dim, vertical, x, y);

        if(place_ship(&player_state->board, &player_state->fleet->ships[i])){
            LOG_ERROR_TAG("Errore durante il piazzamento della nave %d per il giocatore %d", i, player_id);
            is_fleet_valid = 0;
            // break; // Esci dal ciclo se il piazzamento fallisce
        }
    }

    if (player_state->board.ships_left != NUM_SHIPS) {
        LOG_WARNING_TAG("Il giocatore %d ha inviato una flotta incompleta, ignorando la richiesta", player_id);
        is_fleet_valid = 0;
    }

    int dim5 = 0, dim4 = 0, dim3 = 0, dim2 = 0;
    for (int i = 0; i < NUM_SHIPS; i++) {
        int dim = player_state->fleet->ships[i].dim;
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
                current_game_state_type = GAME_IN_PROGRESS;
                update_turn_order(current_game, game_epoll_fd);
            }
        }
    } else {
        LOG_WARNING_TAG("La flotta del giocatore %d non è valida", player_id);
        init_board(&player_state->board);
        free(player_state->fleet);
        player_state->fleet = NULL;

        on_error_player_action_msg(game_epoll_fd, client_s, player_id);
    }
}

/**
 * Gestisce il messaggio di avvio del gioco da parte del proprietario della partita.
 * Verifica se il giocatore è il proprietario della partita e se il gioco è in attesa di giocatori.
 * Se il gioco può essere avviato, invia un messaggio a tutti i giocatori che il gioco è iniziato.
 * Se non tutti i giocatori hanno piazzato le navi, resetta lo stato del gioco e imposta un timer per consentire ai giocatori di piazzare le navi.
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 * @param client_s File descriptor della socket del client.
 * @param player_id ID del giocatore che sta avviando il gioco.
 */
void on_start_game_msg(int game_epoll_fd, int client_s, unsigned int player_id){
    if ((int)player_id == get_game_owner_id(current_game->game_id)) {
        if(current_game_state_type != GAME_WAITING_FOR_PLAYERS){
            LOG_WARNING_TAG("Il giocatore %d ha tentato di avviare il gioco, ma non è in attesa di giocatori", player_id);
            on_unexpected_game_msg(game_epoll_fd, client_s, player_id, MSG_START_GAME);
            return; // Il gioco può essere avviato solo quando è in attesa di giocatori
        }
        // Inizia il gioco
        LOG_INFO_TAG("Il giocatore %d ha iniziato il gioco.", player_id);

        // nessun altro giocatore può unirsi a partire da ora
        set_game_started(current_game->game_id, 1);

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

            update_turn_order(current_game, game_epoll_fd);
        } else {
            LOG_WARNING_TAG("Non tutti i giocatori hanno piazzato le navi, il gioco non può iniziare");
            current_game_state_type = GAME_WAITING_FLEET_SETUP;
            set_epoll_timer(&timer_info, 120); // Imposta il timer a 120 secondi per consentire a chi ancora non ha piazzato le navi di farlo
        }
    } else {
        LOG_ERROR_TAG("Il giocatore %d ha tentato di avviare la partita, ma non ne è il proprietario", player_id);
        on_error_player_action_msg(game_epoll_fd, client_s, player_id);
    }
}

/**
 * Gestisce un messaggio di attacco da parte di un giocatore.
 * Verifica se il gioco è in corso e se il giocatore è il turno del giocatore corrente.
 * Se il giocatore può attaccare, gestisce l'attacco e invia un aggiornamento a tutti i giocatori.
 * Se il gioco non è in corso o il giocatore non è il turno, invia un messaggio di errore.
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 * @param client_s File descriptor della socket del client.
 * @param player_id ID del giocatore che sta attaccando.
 * @param payload Payload del messaggio ricevuto contenente le informazioni sull'attacco.
 */
void on_attack_msg(int game_epoll_fd, int client_s, unsigned int player_id, Payload *payload) {
    if (current_game_state_type != GAME_IN_PROGRESS) {
        LOG_WARNING_TAG("Il giocatore %d ha tentato di attaccare, ma il gioco non è in corso", player_id);
        on_error_player_action_msg(game_epoll_fd, client_s, player_id);
        return; // Continua ad accettare altri messaggi
    }

    if (current_game->player_turn_order[current_game->player_turn] == (int)player_id) {
        // Gestisci l'azione del giocatore
        LOG_DEBUG_TAG("Il giocatore %d ha eseguito un attacco", player_id);

        int attacked_player_id, x, y;
        if(getPayloadIntValue(payload, 0, "player_id", &attacked_player_id) || getPayloadIntValue(payload, 0, "x", &x) || getPayloadIntValue(payload, 0, "y", &y)) {
            LOG_ERROR_TAG("Errore durante l'ottenimento delle coordinate dell'attacco dal payload");
            on_malformed_game_msg(game_epoll_fd, client_s, player_id);
            return; // Continua ad accettare altri messaggi
        }

        PlayerState *attacked_player = get_player_state(current_game, attacked_player_id);
        if (attacked_player == NULL) {
            LOG_ERROR_TAG("Il giocatore %d non esiste nella partita", attacked_player_id);
            on_error_player_action_msg(game_epoll_fd, client_s, player_id);
            return; // Continua ad accettare altri messaggi
        }

        int ret = attack(attacked_player, x, y);
        if (ret < 0) {
            LOG_ERROR_TAG("Errore durante l'attacco del giocatore %d alla posizione (%d, %d)", attacked_player_id, x, y);
            on_error_player_action_msg(game_epoll_fd, client_s, player_id);
            return; // Continua ad accettare altri messaggi
        }

        Payload *attack_payload = createEmptyPayload();
        addPayloadKeyValuePairInt(attack_payload, "attacker_id", player_id);
        addPayloadKeyValuePairInt(attack_payload, "attacked_id", attacked_player_id);
        addPayloadKeyValuePairInt(attack_payload, "x", x);
        addPayloadKeyValuePairInt(attack_payload, "y", y);
        LOG_DEBUG_TAG("Il giocatore %d ha attaccato la posizione (%d, %d) - player %d", player_id, x, y, attacked_player_id);
        if(ret == 0) {
            LOG_DEBUG_TAG("Il giocatore %d ha mancato l'attacco alla posizione (%d, %d) - player %d", player_id, x, y, attacked_player_id);
            addPayloadKeyValuePair(attack_payload, "result", "miss");
        } else if(ret == 1) {
            LOG_DEBUG_TAG("Il giocatore %d ha colpito una nave alla posizione (%d, %d) - player %d", player_id, x, y, attacked_player_id);
            addPayloadKeyValuePair(attack_payload, "result", "hit");
        } else if(ret >= 2) {
            LOG_DEBUG_TAG("Il giocatore %d ha affondato una nave alla posizione (%d, %d) - player %d", player_id, x, y, attacked_player_id);
            addPayloadKeyValuePair(attack_payload, "result", "sunk");
        }
        LOG_DEBUG_TAG("ret: %d", ret);

        send_to_all_players(current_game, MSG_ATTACK_UPDATE, attack_payload, -1);

        if(ret == 3){
            int exists_another_player = 0;
            for(unsigned int i = 0; i < current_game->player_turn_order_count; i++){
                if(current_game->player_turn_order[i] == attacked_player_id) {
                    current_game->player_turn_order[i] = -1; // Rimuove il giocatore attaccato dall'ordine dei turni
                    LOG_INFO_TAG("Il giocatore %d è stato eliminato dalla partita", attacked_player_id);
                }
                if(current_game->player_turn_order[i] != -1 && current_game->player_turn_order[i] != (int)player_id) {
                    exists_another_player = 1; // C'è almeno un altro giocatore nella partita
                }

            }

            if(!exists_another_player) {
                LOG_INFO_TAG("Il giocatore %d ha vinto la partita", player_id);

                Payload *payload = createEmptyPayload();
                addPayloadKeyValuePairInt(payload, "winner_id", player_id);
                send_to_all_players(current_game, MSG_GAME_FINISHED, payload, -1);

                for(unsigned i = 0; i < current_game->players_count; i++) {
                    int client_fd = get_user_socket_fd(current_game->players[i].user.user_id);
                    cleanup_client_game(game_epoll_fd, client_fd, current_game->players[i].user.user_id);
                }
                return;
            }
        }

        update_turn_order(current_game, game_epoll_fd);
    } else {
        LOG_WARNING_TAG("Il giocatore %d ha provato a eseguire un'azione, ma non è il suo turno", player_id);
        if(safeSendMsg(client_s, MSG_ERROR_NOT_YOUR_TURN, NULL) < 0) {
            LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al giocatore %d", player_id);
            cleanup_client_game(game_epoll_fd, client_s, player_id);
        }
    }
}


/**
 * Gestisce un messaggio malformato ricevuto da un client.
 * Invia un messaggio di errore e chiude la connessione del client in caso di errore.
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 * @param client_s File descriptor della socket del client.
 * @param player_id ID dell'utente che ha inviato il messaggio malformato.
 */
void on_malformed_game_msg(int game_epoll_fd, int client_s, unsigned int player_id) {
    LOG_WARNING("Messaggio malformato ricevuto dal client %d.\n", client_s);
    if(safeSendMsg(client_s, MSG_ERROR_MALFORMED_MESSAGE, NULL) < 0){
        LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al client %d", client_s);
        cleanup_client_game(game_epoll_fd, client_s, player_id);
    }
}

/**
 * Gestisce un messaggio non riconosciuto ricevuto da un client.
 * Invia un messaggio di errore e chiude la connessione del client in caso di errore.
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 * @param client_s File descriptor della socket del client.
 * @param player_id ID dell'utente che ha inviato il messaggio malformato.
 * @param msg_type Tipo del messaggio non riconosciuto.
 */
void on_unexpected_game_msg(int game_epoll_fd, int client_s, unsigned int player_id, uint16_t msg_type){
    LOG_WARNING("Messaggio non riconosciuto: %d", msg_type);
    if(safeSendMsg(client_s, MSG_ERROR_UNEXPECTED_MESSAGE, NULL) < 0){
        LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al client %d", client_s);
        cleanup_client_game(game_epoll_fd, client_s, player_id);
    }
}

/**
 * Gestisce un messaggio di errore relativo all'azione del giocatore
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 * @param client_s File descriptor della socket del client.
 * @param player_id ID dell'utente che ha inviato il messaggio malformato.
 */
void on_error_player_action_msg(int game_epoll_fd, int client_s, unsigned int player_id){
    if(safeSendMsg(client_s, MSG_ERROR_PLAYER_ACTION, NULL) < 0) {
        LOG_MSG_ERROR_TAG("Errore durante l'invio del messaggio di errore al giocatore %d", player_id);
        cleanup_client_game(game_epoll_fd, client_s, player_id);
    }
}


/**
 * Invia un messaggio a tutti i giocatori della partita.
 * Utilizza la funzione safeSendMsgWithoutCleanup per inviare il messaggio senza liberare le risorse del payload.
 * Dopo l'invio, libera le risorse del payload.
 * @param game Stato del gioco corrente.
 * @param msg_type Tipo del messaggio da inviare.
 * @param payload Payload da inviare.
 */
void send_to_all_players(GameState *game, uint16_t msg_type, Payload *payload, int except_player_id) {
    for (unsigned int i = 0; i < game->players_count; i++) {
        if((int)game->players[i].user.user_id == except_player_id && except_player_id != -1) continue;

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
}

/**
 * Aggiorna l'ordine dei turni dei giocatori nella partita.
 * Se il gioco non è in corso o non ci sono abbastanza giocatori, gestisce la situazione di errore.
 * Se c'è un solo giocatore, il gioco termina e quel giocatore vince.
 * Altrimenti, genera un nuovo ordine dei turni e invia un messaggio a tutti i giocatori.
 * @param game Stato del gioco corrente.
 * @param game_epoll_fd File descriptor dell'epoll del gioco.
 */
void update_turn_order(GameState *game, int game_epoll_fd){
    if (game == NULL || game->players_count < 2 || current_game_state_type != GAME_IN_PROGRESS) {
        LOG_ERROR_TAG("Stato del gioco non valido o nessun giocatore presente");

        if(game->players_count == 1 && current_game_state_type == GAME_IN_PROGRESS) {
            // Se c'è un solo giocatore, il gioco è finito e quel giocatore vince
            unsigned int player_id = game->players[0].user.user_id;
            LOG_INFO_TAG("Il giocatore %d ha vinto la partita", player_id);

            Payload *payload = createEmptyPayload();
            addPayloadKeyValuePairInt(payload, "winner_id", player_id);
            send_to_all_players(game, MSG_GAME_FINISHED, payload, -1);
        } else {
            LOG_WARNING_TAG("Non ci sono abbastanza giocatori per iniziare il gioco");
        }

        for(unsigned int i = 0; i < current_game->players_count; i++) {
            int client_fd = get_user_socket_fd(current_game->players[i].user.user_id);
            cleanup_client_game(game_epoll_fd, client_fd, current_game->players[i].user.user_id);
        }
    }

    if(game->player_turn_order == NULL){
        generate_turn_order(game);
        game->player_turn = 0; // Inizia dal primo giocatore

        LOG_INFO_TAG("Ordine dei turni generato per la partita %d", game->game_id);
        Payload *payload = createEmptyPayload();
        for(unsigned int i = 0; i < game->player_turn_order_count; i++) {
            addPayloadList(payload);
            addPayloadKeyValuePairInt(payload, "player_id", game->player_turn_order[i]);
        }

        send_to_all_players(game, MSG_GAME_STARTED, payload, -1);
    }

    while(1){
        game->player_turn = (game->player_turn + 1) % game->player_turn_order_count;
        if(game->player_turn_order[game->player_turn] == -1) continue;

        int conn_s = get_user_socket_fd(game->player_turn_order[game->player_turn]);
        if (conn_s < 0) {
            LOG_ERROR_TAG("Impossibile ottenere il file descriptor per il giocatore %d", game->player_turn_order[game->player_turn]);
            game->player_turn_order[game->player_turn] = -1;
            continue; // Errore, impossibile ottenere il file descriptor
        }

        Payload *turn_payload = createEmptyPayload();
        addPayloadKeyValuePairInt(turn_payload, "player_turn", game->player_turn);
        send_to_all_players(game, MSG_TURN_ORDER_UPDATE, turn_payload, game->player_turn_order[game->player_turn]);

        if (safeSendMsg(conn_s, MSG_YOUR_TURN, NULL) < 0) {
            LOG_ERROR_TAG("Errore durante l'invio del messaggio di turno al giocatore %d", game->player_turn_order[game->player_turn]);
            game->player_turn_order[game->player_turn] = -1;
            cleanup_client_game(game_epoll_fd, conn_s, game->player_turn_order[game->player_turn]);
            continue; // Errore, impossibile inviare il messaggio
        }

        LOG_INFO_TAG("È il turno del giocatore %d", game->player_turn_order[game->player_turn]);
        set_epoll_timer(&timer_info, 60); // Resetta il timer a 60 secondi per il prossimo turno
        break;
    }

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
    if(client_fd != -1) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        close(client_fd);
    }

    for(unsigned int i = 0; i < current_game->player_turn_order_count; i++) {
        if(current_game->player_turn_order[i] == (int)player_id) {
            current_game->player_turn_order[i] = -1; // Rimuove il giocatore dall'ordine dei turni
            LOG_INFO_TAG("Il giocatore %d è stato rimosso dall'ordine dei turni", player_id);
            break;
        }
    }
    remove_user(player_id); // Rimuove l'utente dalla lista degli utenti
    LOG_INFO_TAG("Utente %d disconnesso e rimosso", player_id);

    remove_player_from_game_state(current_game, player_id);

    if(current_game->players_count == 0) {
        // Se non ci sono più giocatori, distruggi lo stato del gioco
        LOG_INFO_TAG("Non ci sono più giocatori nella partita, procedo a eliminarla");
        remove_game(current_game->game_id); // Rimuovi la partita dallo stato del server
        LOG_INFO_TAG("Partita %d rimossa dallo stato del server", current_game->game_id);

        free_game_state(current_game);
        pthread_exit(NULL); // Termina il thread di gioco
    }

    Payload *payload = createEmptyPayload();
    addPayloadKeyValuePairInt(payload, "player_id", player_id);
    send_to_all_players(current_game, MSG_PLAYER_LEFT, payload, -1);
    // TODO dovrei evitare di rimuovere il giocatore per permettere la riconnessione
}


void set_epoll_timer(TimerInfo *timer_info, int duration){
    time_t current_time = time(NULL);
    timer_info->start_time = current_time;
    timer_info->duration = duration;
}

int get_epoll_timer(TimerInfo *timer_info) {
    if (timer_info->duration <= 0) {
        return -1; // Nessun timer attivo
    }

    time_t current_time = time(NULL);
    int elapsed_time = current_time - timer_info->start_time;
    int remaining_time = timer_info->duration - elapsed_time;

    if (remaining_time < 0) {
        timer_info->duration = -1;
        remaining_time = 0; // Il timer è scaduto
    }
    return remaining_time;
}
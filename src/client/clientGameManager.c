#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/epoll.h>

#include "clientGameManager.h"
#include "client/gameUI.h"
#include "utils/debug.h"
#include "utils/userInput.h"
#include "common/protocol.h"
#include "common/game.h"

UserInfo *user = NULL; // Informazioni sull'utente corrente
int is_owner = 0; // Indica se l'utente è il proprietario della partita
int local_player_turn_index = -1; // Index del turno del giocatore locale (utente di questo client)

GameState *game = NULL;
pthread_mutex_t game_state_mutex;

FILE *client_log_file = NULL;
char *log_file_path = NULL;

void handle_game_msg(int conn_s, unsigned int game_id, char *game_name) {
    game = create_game_state(game_id, game_name);
    if (game == NULL) {
        LOG_ERROR("Errore nella creazione dello stato di gioco");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&game_state_mutex, NULL);

    pthread_mutex_lock(&game_state_mutex);
    if(add_player_to_game_state(game, user->user_id, user->username)) {
        LOG_ERROR("Errore nell'aggiunta del giocatore allo stato di gioco");
        exit(EXIT_FAILURE);
    }

    PlayerState *player_state = get_player_state(game, user->user_id);
    player_state->fleet = (FleetSetup *)malloc(sizeof(FleetSetup));
    if (player_state->fleet == NULL) {
        LOG_ERROR("Errore nell'allocazione della flotta del giocatore");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&game_state_mutex);

    asprintf(&log_file_path, "client_game_%d.log", getpid());
    client_log_file = fopen(log_file_path, "w+");
    if (!client_log_file) {
        LOG_ERROR("Errore nell'apertura del file di log %s, usando stderr", log_file_path);
        client_log_file = stderr;
    } else {
        atexit(print_log_file);
    }

    LOG_INFO_FILE(client_log_file, "Iniziando la partita `%s` con ID %d", game_name, game_id);

    // Avvio il thread dell'interfaccia di gioco
    // NOTA: da questo punto in poi, il thread dell'interfaccia di gioco gestirà l'input dell'utente
    // e per printare i messaggi di gioco è necessario utilizzare la funzione `log_game_message`
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGWINCH); // Blocca SIGWINCH in questo thread
    sigaddset(&set, SIGINT); // Blocca SIGINT in questo thread
    sigaddset(&set, SIGTERM); // Blocca SIGTERM in questo thread
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    LOG_INFO_FILE(client_log_file, "Inizializzazione dell'interfaccia di gioco");
    init_game_interface();

    LOG_INFO_FILE(client_log_file, "Avvio del thread dell'interfaccia di gioco");
    GameUIArg *ui_arg = malloc(sizeof(GameUIArg));
    if (!ui_arg) {
        LOG_ERROR_FILE(client_log_file, "Errore durante l'allocazione della struttura GameUIArg");
        exit(EXIT_FAILURE);
    }
    int pipe_fd_game_ui[2];
    if (pipe(pipe_fd_game_ui) < 0) {
        LOG_ERROR_FILE(client_log_file, "Errore durante la creazione della pipe per l'interfaccia di gioco");
        exit(EXIT_FAILURE);
    }
    ui_arg->pipe_fd_write = pipe_fd_game_ui[1];

    pthread_t game_thread_id;
    if(pthread_create(&game_thread_id, NULL, game_ui_thread, (void *)ui_arg) != 0) {
        LOG_ERROR_FILE(client_log_file, "Errore durante la creazione del thread di gioco");
        exit(EXIT_FAILURE);
    }
    
    LOG_INFO_FILE(client_log_file, "Do il benvenuto al giocatore `%s` nella partita `%s` con ID %d", user->username, game_name, game_id);
    log_game_message("Benvenuto nella partita `%s` con ID %d", game_name, game_id);

    LOG_DEBUG_FILE(client_log_file, "Attendo che il server mi invii le informazioni sulla partita");
    if(safeSendMsg(conn_s, MSG_READY_TO_PLAY, NULL)){
        LOG_ERROR_FILE(client_log_file, "Errore durante l'invio di MSG_READY_TO_PLAY al server");
        exit(EXIT_FAILURE);
    }

    int client_epoll_fd = epoll_create1(0);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = pipe_fd_game_ui[0];
    epoll_ctl(client_epoll_fd, EPOLL_CTL_ADD, pipe_fd_game_ui[0], &ev);

    ev.events = EPOLLIN;
    ev.data.fd = conn_s;
    epoll_ctl(client_epoll_fd, EPOLL_CTL_ADD, conn_s, &ev);

    while(1) {
        struct epoll_event events[2];
        int nfds = epoll_wait(client_epoll_fd, events, 1, -1);
        if (nfds < 0) {
            LOG_ERROR_FILE(client_log_file, "Errore durante l'attesa di eventi sull'epoll");
            break;
        }

        if (events[0].data.fd == pipe_fd_game_ui[0]) {
            // Riceve i segnali dall'interfaccia di gioco
            GameUISignal signal;
            if (read(pipe_fd_game_ui[0], &signal, sizeof(GameUISignal)) < 0) {
                LOG_ERROR_FILE(client_log_file, "Errore durante la lettura dalla pipe dell'interfaccia di gioco");
                break;
            }

            switch (signal.type) {
                case GAME_UI_SIGNAL_FLEET_DEPLOYED:{
                    // Invia un messaggio al server per notificare che la flotta è stata piazzata
                    Payload *payload = createEmptyPayload();
                    pthread_mutex_lock(&game_state_mutex);
                    for(int i = 0; i < NUM_SHIPS; i++) {
                        ShipPlacement *ship = &game->players[0].fleet->ships[i];

                        addPayloadList(payload);
                        addPayloadKeyValuePairInt(payload, "dim", ship->dim);
                        addPayloadKeyValuePairInt(payload, "vertical", ship->vertical);
                        addPayloadKeyValuePairInt(payload, "x", ship->x);
                        addPayloadKeyValuePairInt(payload, "y", ship->y);
                    }
                    pthread_mutex_unlock(&game_state_mutex);
                    if (safeSendMsg(conn_s, MSG_SETUP_FLEET, payload) < 0) {
                        LOG_ERROR_FILE(client_log_file, "Errore durante l'invio del messaggio MSG_SETUP_FLEET al server");
                        goto close_game;
                    }
                    break;
                }
                case GAME_UI_SIGNAL_START_GAME:{
                    // Invia un messaggio al server per notificare che il gioco può iniziare
                    if (safeSendMsg(conn_s, MSG_START_GAME, NULL) < 0) {
                        LOG_ERROR_FILE(client_log_file, "Errore durante l'invio del messaggio MSG_START_GAME al server");
                        goto close_game;
                    }
                    break;
                }
                case GAME_UI_SIGNAL_ATTACK:{
                    pthread_mutex_lock(&game_state_mutex);
                    AttackPosition *attack_position = (AttackPosition *)signal.data;

                    Payload *attack_payload = createEmptyPayload();
                    addPayloadKeyValuePairInt(attack_payload, "player_id", attack_position->player_id);
                    addPayloadKeyValuePairInt(attack_payload, "x", attack_position->x);
                    addPayloadKeyValuePairInt(attack_payload, "y", attack_position->y);

                    log_game_message("Attacco in corso contro il giocatore %d alla posizione (%d, %d)", attack_position->player_id, attack_position->x, attack_position->y);
                    
                    free(attack_position);

                    if (safeSendMsg(conn_s, MSG_ATTACK, attack_payload) < 0) {
                        LOG_ERROR_FILE(client_log_file, "Errore durante l'invio del messaggio MSG_PLAYER_ACTION al server");
                    }
                    pthread_mutex_unlock(&game_state_mutex);
                    break;
                }
                default:
                    LOG_WARNING_FILE(client_log_file, "Segnale non gestito: %d", signal.type);
                    break;

            }

        } else if (events[0].data.fd == conn_s) {
            // Riceve i messaggi dal server

            uint16_t msg_type;
            Payload *payload = NULL;
            if (safeRecvMsg(conn_s, &msg_type, &payload) < 0) {
                LOG_ERROR_FILE(client_log_file, "Errore durante la ricezione del messaggio di gioco dal server");
                break;
            }

            LOG_DEBUG_FILE(client_log_file, "Ricevuto messaggio di gioco: %d", msg_type);

            switch (msg_type) {
                case MSG_GAME_STATE_UPDATE: {
                    on_game_state_update_msg(payload);
                    break;
                }
                case MSG_PLAYER_JOINED: {
                    on_player_joined_msg(payload);
                    break;
                }
                case MSG_PLAYER_LEFT: {
                    on_player_left_msg(payload);
                    break;
                }
                case MSG_GAME_STARTED: {
                    on_game_started_msg(payload);
                    break;
                }
                case MSG_TURN_ORDER_UPDATE: {
                    on_turn_order_update_msg(payload);
                    break;
                }
                case MSG_YOUR_TURN: {
                    on_your_turn_msg();
                    break;
                }
                case MSG_ATTACK_UPDATE: {
                    on_attack_update_msg(payload);
                    break;
                }
                case MSG_GAME_FINISHED: {
                    on_game_finished_msg(payload);
                    break;
                }
                default:
                    handle_generic_msg(msg_type);
                    break;
            }

            freePayload(payload);
        }
    }

close_game:
    LOG_INFO_FILE(client_log_file, "Chiusura della partita `%s` con ID %d", game_name, game_id);
    pthread_cancel(game_thread_id); // Cancella il thread dell'interfaccia di gioco
    pthread_join(game_thread_id, NULL); // Attende la terminazione del thread dell'interfaccia di gioco
    exit(0);
}


void on_game_state_update_msg(Payload *payload) {
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_GAME_STATE_UPDATE");
    // Gestisci l'aggiornamento dello stato del gioco
    int payload_size = getPayloadListSize(payload);
    for(int i = 0; i < payload_size; i++) {
        char *key = getPayloadValue(payload, i, "type");
        if (key == NULL) {
            LOG_ERROR_FILE(client_log_file, "Tipo di messaggio non specificato nel payload");
            continue;
        }

        if (strcmp(key, "game_info") == 0) {
            // Gestisci le informazioni del gioco
        } else if (strcmp(key, "player_info") == 0) {
            // Gestisci le informazioni del giocatore
            int player_id;
            if (getPayloadIntValue(payload, i, "player_id", &player_id) < 0) {
                LOG_ERROR_FILE(client_log_file, "ID del giocatore non trovato nel payload");
                free(key);
                continue;
            }
            char *username = getPayloadValue(payload, i, "username");
            if (username == NULL) {
                LOG_ERROR_FILE(client_log_file, "Nome utente non trovato nel payload");
                free(key);
                continue;
            }
            pthread_mutex_lock(&game_state_mutex);
            int game_id_local = game->game_id;
            add_player_to_game_state(game, player_id, username);
            pthread_mutex_unlock(&game_state_mutex);
            log_game_message("Giocatore %d (`%s`) si è unito alla partita %d", player_id, username, game_id_local);

            free(username);
        }

        free(key);
    }
}

void on_player_joined_msg(Payload *payload){
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_PLAYER_JOINED");
    int player_id;
    if (getPayloadIntValue(payload, 0, "player_id", &player_id) < 0) {
        LOG_ERROR_FILE(client_log_file, "ID del giocatore non trovato nel payload");
        return;
    }
    char *username = getPayloadValue(payload, 0, "username");
    if(username == NULL) {
        LOG_ERROR_FILE(client_log_file, "Nome utente non trovato nel payload");
        return;
    }

    pthread_mutex_lock(&game_state_mutex);
    int game_id_local = game->game_id;
    add_player_to_game_state(game, player_id, username);
    pthread_mutex_unlock(&game_state_mutex);
    log_game_message("Giocatore %d (`%s`) si è unito alla partita %d", player_id, username, game_id_local);

    free(username);

}

void on_player_left_msg(Payload *payload) {
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_PLAYER_LEFT");
    int player_id;
    if (getPayloadIntValue(payload, 0, "player_id", &player_id) < 0) {
        LOG_ERROR_FILE(client_log_file, "ID del giocatore non trovato nel payload");
        return;
    }

    pthread_mutex_lock(&game_state_mutex);
    for(unsigned int i = 0; i < game->player_turn_order_count; i++) {
        if(game->player_turn_order[i] == (int)player_id) {
            game->player_turn_order[i] = -1; // Rimuove il giocatore dall'ordine dei turni
            LOG_DEBUG_FILE(client_log_file, "Il giocatore %d è stato rimosso dall'ordine dei turni", player_id);
            break;
        }
    }

    PlayerState *player_state = get_player_state(game, player_id);
    if (player_state == NULL) {
        LOG_ERROR_FILE(client_log_file, "Stato del giocatore non trovato");
        pthread_mutex_unlock(&game_state_mutex);
        return;
    }
    int game_id_local = game->game_id;
    char *username_local = player_state->user.username ? strdup(player_state->user.username) : NULL;
    remove_player_from_game_state(game, player_id);
    pthread_mutex_unlock(&game_state_mutex);
    log_game_message("Il giocatore %d (`%s`) ha lasciato la partita %d", player_id, username_local ? username_local : "Unknown", game_id_local);
    free(username_local);
    
}

void on_game_started_msg(Payload *payload) {
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_GAME_STARTED");

    int payload_size = getPayloadListSize(payload);
    if (payload_size <= 0) {
        LOG_ERROR_FILE(client_log_file, "Nessun giocatore trovato nell'ordine dei turni");
        return;
    }

    pthread_mutex_lock(&game_state_mutex);
    game->player_turn_order_count = payload_size;
    game->player_turn_order = realloc(game->player_turn_order, sizeof(int) * payload_size);
    if (game->player_turn_order == NULL) {
        LOG_ERROR_FILE(client_log_file, "Errore durante la riallocazione dell'array di ordine dei turni");
        pthread_mutex_unlock(&game_state_mutex);
        return;
    }

    for (int i = 0; i < payload_size; i++) {
        int player_id;
        if (getPayloadIntValue(payload, i, "player_id", &player_id) < 0) {
            LOG_ERROR_FILE(client_log_file, "ID del giocatore non trovato nel payload");
            continue;
        }
        game->player_turn_order[i] = player_id;
        if (player_id == (int)user->user_id) {
            local_player_turn_index = i;
        }
    }

    char *game_name_local = game->game_name ? strdup(game->game_name) : NULL;
    int game_id_local = game->game_id;
    pthread_mutex_unlock(&game_state_mutex);
    log_game_message("La partita `%s` con ID %d è iniziata!", game_name_local ? game_name_local : "?", game_id_local);
    free(game_name_local);

    pthread_mutex_lock(&screen.mutex);
    screen.game_screen_state = GAME_SCREEN_STATE_PLAYING;
    screen.current_showed_player = 0;
    pthread_mutex_lock(&game_state_mutex);
    do{
        screen.current_showed_player = (screen.current_showed_player + 1) % game->player_turn_order_count;
    } while(game->player_turn_order[screen.current_showed_player] == -1 || (int)screen.current_showed_player == local_player_turn_index);
    pthread_mutex_unlock(&game_state_mutex);
    screen.cursor.show = 1;
    pthread_mutex_unlock(&screen.mutex);

    refresh_screen();
}

void on_turn_order_update_msg(Payload *payload){
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_TURN_ORDER_UPDATE");

    int player_turn;
    if (getPayloadIntValue(payload, 0, "player_turn", &player_turn) < 0) {
        LOG_ERROR_FILE(client_log_file, "Turno del giocatore non trovato nel payload");
        return;
    }
    
    pthread_mutex_lock(&game_state_mutex);
    game->player_turn = player_turn;
    int current_player_id = game->player_turn_order[player_turn];
    pthread_mutex_unlock(&game_state_mutex);
    log_game_message("È il turno del giocatore %d", current_player_id);
}

void on_your_turn_msg(){
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_YOUR_TURN");

    log_game_message("È il tuo turno di giocare! Effettua la tua mossa...");
    pthread_mutex_lock(&game_state_mutex);
    game->player_turn = local_player_turn_index; // Imposta il turno del giocatore corrente a local_player_turn_index
    pthread_mutex_unlock(&game_state_mutex);
}

void on_attack_update_msg(Payload *payload) {
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_ATTACK_UPDATE");

    int attacker_id, attacked_id, x, y;
    if (getPayloadIntValue(payload, 0, "attacker_id", &attacker_id) < 0 ||
        getPayloadIntValue(payload, 0, "attacked_id", &attacked_id) < 0 ||
        getPayloadIntValue(payload, 0, "x", &x) < 0 ||
        getPayloadIntValue(payload, 0, "y", &y) < 0) {
        LOG_ERROR_FILE(client_log_file, "Informazioni sull'attacco non trovate nel payload");
        return;
    }
    char *result = getPayloadValue(payload, 0, "result");
    if (result == NULL) {
        LOG_ERROR_FILE(client_log_file, "Risultato dell'attacco non trovato nel payload");
        return;
    }

    pthread_mutex_lock(&game_state_mutex);
    PlayerState *attacked_state = get_player_state(game, attacked_id);
    if (attacked_state == NULL) {
        LOG_ERROR_FILE(client_log_file, "Giocatore con ID %d non trovato nello stato del gioco", attacker_id);
        free(result);
        pthread_mutex_unlock(&game_state_mutex);
        return;
    }
    GameBoard *attacked_board = &attacked_state->board;
    int log_case = 0; // 1=hit,2=miss,3=sunk,0=unknown
    if(strcmp(result, "hit") == 0) {
        set_cell(attacked_board, x, y, 'X'); // Colpito
        log_case = 1;
    } else if (strcmp(result, "miss") == 0) {
        set_cell(attacked_board, x, y, '*'); // Mancato
        log_case = 2;
    } else if (strcmp(result, "sunk") == 0) {
        set_cell(attacked_board, x, y, 'X'); // Colpito
        attacked_board->ships_left--; // Decrementa le navi rimaste
        log_case = 3;
    } else {
        LOG_ERROR_FILE(client_log_file, "Risultato dell'attacco non riconosciuto: %s", result);
    }
    pthread_mutex_unlock(&game_state_mutex);

    if (log_case == 1) {
        log_game_message("Il giocatore %d ha colpito la posizione (%d, %d)", attacker_id, x, y);
    } else if (log_case == 2) {
        log_game_message("Il giocatore %d ha mancato la posizione (%d, %d)", attacker_id, x, y);
    } else if (log_case == 3) {
        log_game_message("Il giocatore %d ha affondato una nave alla posizione (%d, %d)", attacker_id, x, y);
    }

    free(result);

}

void on_game_finished_msg(Payload *payload) {
    LOG_DEBUG_FILE(client_log_file, "Ricevuto MSG_GAME_FINISHED");

    int winner_id;
    if (getPayloadIntValue(payload, 0, "winner_id", &winner_id) < 0) {
        LOG_ERROR_FILE(client_log_file, "ID del vincitore non trovato nel payload");
        return;
    }

    pthread_mutex_lock(&screen.mutex);
    screen.game_screen_state = GAME_SCREEN_STATE_FINISHED;
    pthread_mutex_unlock(&screen.mutex);

    if(winner_id == (int)user->user_id) {
        log_game_message("La partita è finita! Hai vinto!");
    } else {
        log_game_message("La partita è finita! Il vincitore è il giocatore %d", winner_id);
    }

    sleep(15);

}


/**
 * Gestisce i messaggi generici ricevuti dal server.
 * @param msg_type Il tipo di messaggio.
 * @param payload Il payload del messaggio.
 */
void handle_generic_msg(uint16_t msg_type) {

    switch(msg_type) {
        case MSG_ERROR_UNEXPECTED_MESSAGE:
            LOG_ERROR_FILE(client_log_file, "Messaggio non riconosciuto ricevuto dal server");
            break;

        case MSG_ERROR_MALFORMED_MESSAGE:
            LOG_ERROR_FILE(client_log_file, "Messaggio malformato ricevuto dal server");
            break;

        case MSG_ERROR_NOT_AUTHENTICATED:
            LOG_ERROR_FILE(client_log_file, "Messaggio di errore: non autenticato");
            break;

        default:
            LOG_WARNING_FILE(client_log_file, "Messaggio non riconosciuto: %d", msg_type);
            break;
    }
}

void print_log_file() {
    if (client_log_file && client_log_file != stderr) {
        fseek(client_log_file, 0, SEEK_SET);

        char buffer[256];
        LOG_INFO("Contenuto del file di log client.log:");
        while (fgets(buffer, sizeof(buffer), client_log_file) != NULL) {
            printf("%s", buffer);
        }

        fclose(client_log_file);

        remove(log_file_path); // Elimina il file di log dopo la stampa
        client_log_file = NULL;
        free(log_file_path);
        log_file_path = NULL;
    }
    LOG_INFO("File di log chiuso correttamente.");
}
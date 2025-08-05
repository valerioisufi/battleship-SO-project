#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <sys/socket.h>     // socket definitions
#include <sys/types.h>      // socket types
#include <arpa/inet.h>      // inet (3) functions
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>
#include <sys/epoll.h>
#include <errno.h>

#include "server/lobbyManager.h"
#include "utils/cmdLineParser.h"
#include "utils/debug.h"
#include "common/protocol.h"
#include "server/users.h"


/**
 * Funzione eseguita dal thread della lobby per gestire le connessioni dei client.
 */
#define MAX_EVENTS 128
void *lobby_thread_main(void *arg) {
    int lobby_pipe_fd = *(int *)arg;
    int lobby_epoll_fd = epoll_create1(0);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = UINT64_MAX; // Indica che è un evento di connessione
    epoll_ctl(lobby_epoll_fd, EPOLL_CTL_ADD, lobby_pipe_fd, &ev);

    while (1) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(lobby_epoll_fd, events, MAX_EVENTS, -1);

        for(int n = 0; n < nfds; n++){
            if(events[n].data.u64 == UINT64_MAX) {
                int new_conn_s;
                if (read(lobby_pipe_fd, &new_conn_s, sizeof(new_conn_s)) == -1) {
                    LOG_ERROR("Errore durante la lettura dalla pipe della lobby");
                    continue; // Continua ad accettare altre connessioni
                }

                int user_id = create_user(NULL, new_conn_s);
                if(user_id < 0) {
                    LOG_WARNING("Errore nella creazione dell'utente per la connessione %d", new_conn_s);
                    close(new_conn_s);
                    continue; // Continua ad accettare altre connessioni
                }

                ev.events = EPOLLIN;
                ev.data.u64 = user_id;
                epoll_ctl(lobby_epoll_fd, EPOLL_CTL_ADD, new_conn_s, &ev);

            }else{
                int user_id = events[n].data.u64;
                int client_s = get_user_socket_fd(user_id);
                if (client_s < 0) {
                    LOG_WARNING("Errore nell'ottenimento della socket per il giocatore %d", user_id);
                    continue; // Continua ad accettare altri messaggi
                }

                uint16_t msg_type;
                Payload *payload = NULL;
                if(safeRecvMsg(client_s, &msg_type, &payload) < 0){
                    LOG_MSG_ERROR("Errore durante la ricezione del messaggio dal client %d, procedo a chiuderne la connessione...", client_s);
                    cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
                    continue; // Continua ad accettare altri messaggi
                }

                switch(msg_type){
                    case MSG_LOGIN:
                        // Gestione del login
                        on_login_msg(lobby_epoll_fd, user_id, client_s, payload);
                        break;

                    case MSG_CREATE_GAME:
                        // Gestione della creazione di una nuova partita
                        on_create_game_msg(lobby_epoll_fd, user_id, client_s, payload);
                        break;

                    case MSG_JOIN_GAME:
                        // Gestione dell'unione a una partita
                        LOG_DEBUG("Il giocatore %d ha inviato un messaggio di unione a una partita", user_id);
                        on_join_game_msg(lobby_epoll_fd, user_id, client_s, payload);
                        break;
                        
                    default:
                        on_unexpected_msg(lobby_epoll_fd, user_id, client_s, msg_type);
                        break;
                }

                freePayload(payload);
                
            }
        }
    }

    return NULL;
}

/**
 * Rimuove un client dal set epoll e chiude la relativa socket.
 * Utile per gestire la disconnessione e il cleanup di risorse associate a un client.
 * @param epoll_fd File descriptor dell'epoll.
 * @param client_fd File descriptor della socket del client da chiudere.
 * @param user_id ID dell'utente da rimuovere.
 */
void cleanup_client_lobby(int epoll_fd, int client_fd, unsigned int user_id) {
    // TODO da rivedere
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
    remove_user(user_id); // Rimuove l'utente dalla lista degli utenti
    LOG_INFO("Utente %d disconnesso e rimosso", user_id);
}


/**
 * Gestisce il messaggio di login da parte di un client.
 * Se l'autenticazione va a buon fine, aggiorna il nome utente e invia un messaggio di benvenuto.
 * Se l'autenticazione fallisce, invia un messaggio di errore.
 * @param lobby_epoll_fd File descriptor dell'epoll della lobby.
 * @param user_id ID dell'utente che sta effettuando il login.
 * @param client_s File descriptor della socket del client.
 * @param payload Payload del messaggio ricevuto.
 */
void on_login_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, Payload *payload) {
    char *username = getPayloadValue(payload, 0, "username");

    if (username) {
        LOG_INFO("Utente `%s` si è connesso", username);

        if(update_user_username(user_id, username) < 0){
            LOG_ERROR("Errore durante l'aggiornamento del nome utente per l'utente %d", user_id);
            cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
            free(username);
            goto cleanup;
        }

        Payload *welcomePayload = createEmptyPayload();
        addPayloadKeyValuePair(welcomePayload, "username", username);
        addPayloadKeyValuePairInt(welcomePayload, "user_id", user_id);
        if(safeSendMsg(client_s, MSG_WELCOME, welcomePayload) < 0){
            LOG_MSG_ERROR("Errore durante l'invio del messaggio di benvenuto a `%s`", username);
            cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
            goto cleanup;
        }

        LOG_INFO("Messaggio di benvenuto inviato a `%s`", username);
    } else {
        LOG_WARNING("Messaggio di login non valido, nome utente mancante");
        on_malformed_msg(lobby_epoll_fd, user_id, client_s);
    }


cleanup:
    free(username);
}

/**
 * Gestisce il messaggio di creazione di una nuova partita.
 * Crea una nuova partita e invia un messaggio di conferma al client.
 * Se la creazione della partita fallisce, invia un messaggio di errore.
 * @param lobby_epoll_fd File descriptor dell'epoll della lobby.
 * @param user_id ID dell'utente che sta creando la partita.
 * @param client_s File descriptor della socket del client.
 * @param payload Payload del messaggio ricevuto.
 */
void on_create_game_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, Payload *payload) {
    char *username = require_authentication(lobby_epoll_fd, user_id, client_s);
    if (!username) {
        return; // Client non autenticato, già gestito in require_authentication
    }

    char *game_name = getPayloadValue(payload, 0, "game_name");

    if(game_name){
        int game_id = create_game(game_name, user_id);

        if(game_id < 0){
            LOG_ERROR("Errore durante la creazione della partita per l'utente `%s`", username);
            if(safeSendMsg(client_s, MSG_ERROR_CREATE_GAME, NULL) < 0){
                LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al client `%s`", username);
                cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
                goto cleanup;
            }
        } else {
            LOG_INFO("Partita '%s' creata con ID %d da `%s`", game_name, game_id, username);
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", game_id);

            Payload *payload = createEmptyPayload();
            addPayloadKeyValuePair(payload, "game_id", buffer);

            if(safeSendMsg(client_s, MSG_GAME_CREATED, payload) < 0){
                LOG_MSG_ERROR("Errore durante l'invio del messaggio di partita creata al client `%s`", username);
                cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
                goto cleanup;
            }
            epoll_ctl(lobby_epoll_fd, EPOLL_CTL_DEL, client_s, NULL);
        }
    } else {
        LOG_WARNING("Nome della partita non fornito");
        on_malformed_msg(lobby_epoll_fd, user_id, client_s);
    }

cleanup:
    free(username);
    free(game_name);
}

/**
 * Gestisce il messaggio di unione a una partita.
 * Aggiunge il giocatore alla partita specificata e invia un messaggio di conferma al client.
 * Se l'unione alla partita fallisce, invia un messaggio di errore.
 * @param lobby_epoll_fd File descriptor dell'epoll della lobby.
 * @param user_id ID dell'utente che sta unendosi alla partita.
 * @param client_s File descriptor della socket del client.
 * @param payload Payload del messaggio ricevuto.
 */
void on_join_game_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, Payload *payload){
    char *username = require_authentication(lobby_epoll_fd, user_id, client_s);
    if (!username) {
        return; // Client non autenticato, già gestito in require_authentication
    }

    char *game_id_str = getPayloadValue(payload, 0, "game_id");

    if(game_id_str){
        char *endptr;
        unsigned long tmp = strtoul(game_id_str, &endptr, 10);
        if (*endptr != '\0' || game_id_str[0] == '\0') {
            // Errore di conversione
            LOG_WARNING("ID della partita non valido: `%s`", game_id_str);
            on_malformed_msg(lobby_epoll_fd, user_id, client_s);
            goto cleanup;
        }
        unsigned int game_id = (unsigned int)tmp;

        if(add_player_to_game(game_id, user_id) == 0){
            char *game_name = get_game_name_by_id(game_id);
            LOG_INFO("Utente %d:`%s` si è unito alla partita %d:`%s`", user_id, username, game_id, game_name);
            Payload *joinGamePayload = createEmptyPayload();
            addPayloadKeyValuePair(joinGamePayload, "game_name", game_name);
            if(safeSendMsg(client_s, MSG_GAME_JOINED, joinGamePayload) < 0){
                LOG_MSG_ERROR("Errore durante l'invio del messaggio di partita unita al client %d:`%s`", user_id, username);
                cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
                goto cleanup;
            }
            free(game_name);
            epoll_ctl(lobby_epoll_fd, EPOLL_CTL_DEL, client_s, NULL);
        } else {
            LOG_ERROR("Errore durante l'unione alla partita %d per l'utente %d.`%s`", game_id, user_id, username);
            if(safeSendMsg(client_s, MSG_ERROR_JOIN_GAME, NULL) < 0){
                LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al client %d:`%s`", user_id, username);
                cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
                goto cleanup;
            }
        }
    } else {
        LOG_WARNING("ID della partita non fornito.\n");
        on_malformed_msg(lobby_epoll_fd, user_id, client_s);
        goto cleanup;
    }

cleanup:
    free(username);
    free(game_id_str);
}


/**
 * Verifica se un client è autenticato prima di procedere con l'elaborazione del messaggio.
 * Se il client non è autenticato, invia un messaggio di errore e chiude la connessione.
 * @param lobby_epoll_fd File descriptor dell'epoll della lobby.
 * @param user_id ID dell'utente da verificare.
 * @param client_s File descriptor della socket del client.
 * @return Puntatore al nome utente se autenticato, altrimenti NULL.
 */
char *require_authentication(int lobby_epoll_fd, unsigned int user_id, int client_s) {
    char *username = get_username_by_id(user_id);

    if(!username){
        LOG_WARNING("Client %d non autenticato", client_s);
        if(safeSendMsg(client_s, MSG_ERROR_NOT_AUTHENTICATED, NULL) < 0){
            LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al client %d", client_s);
            cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
        }
    }
    return username;
}

/**
 * Gestisce un messaggio malformato ricevuto da un client.
 * Invia un messaggio di errore e chiude la connessione del client in caso di errore.
 * @param lobby_epoll_fd File descriptor dell'epoll della lobby.
 * @param user_id ID dell'utente che ha inviato il messaggio malformato.
 * @param client_s File descriptor della socket del client.
 * @return 0 se l'operazione è andata a buon fine, -1 in caso di errore.
 */
int on_malformed_msg(int lobby_epoll_fd, unsigned int user_id, int client_s) {
    // LOG_WARNING("Messaggio malformato ricevuto dal client %d.\n", client_s);
    if(safeSendMsg(client_s, MSG_ERROR_MALFORMED_MESSAGE, NULL) < 0){
        LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al client %d", client_s);
        cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
        return -1;
    }
    return 0;
}

/**
 * Gestisce un messaggio non riconosciuto ricevuto da un client.
 * Invia un messaggio di errore e chiude la connessione del client in caso di errore.
 * @param lobby_epoll_fd File descriptor dell'epoll della lobby.
 * @param user_id ID dell'utente che ha inviato il messaggio non riconosciuto.
 * @param client_s File descriptor della socket del client.
 * @param msg_type Tipo del messaggio non riconosciuto.
 */
void on_unexpected_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, uint16_t msg_type){
    LOG_WARNING("Messaggio non riconosciuto: %d", msg_type);
    if(safeSendMsg(client_s, MSG_ERROR_UNEXPECTED_MESSAGE, NULL) < 0){
        LOG_MSG_ERROR("Errore durante l'invio del messaggio di errore al client %d", client_s);
        cleanup_client_lobby(lobby_epoll_fd, client_s, user_id);
    }
}
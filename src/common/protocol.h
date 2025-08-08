#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/**
 * PlayerMsgType:
 * Enumera i tipi di messaggi che possono essere inviati dal client al server.
 */
typedef enum {
    MSG_LOGIN,                      // Il client invia questo messaggio per autenticarsi o registrarsi al server, fornendo il proprio username.
    MSG_CREATE_GAME,                // Il client richiede la creazione di una nuova partita, specificando il nome della partita.
    MSG_JOIN_GAME,                  // Il client richiede di unirsi a una partita esistente, fornendo l'ID della partita.

    MSG_LEAVE_GAME,                 // Il client comunica l'intenzione di abbandonare la partita corrente.
    MSG_READY_TO_PLAY,              // Il client segnala di aver completato la configurazione e di essere pronto a ricevere dati sulla partita.
    MSG_START_GAME,                 // Il proprietario della partita invia questo messaggio per avviare la partita quando tutti sono pronti.
    MSG_ATTACK,                     // Il client effettua una mossa di attacco, specificando le coordinate e il bersaglio.
    MSG_SETUP_FLEET                 // Il client invia la configurazione della propria flotta (posizionamento delle navi) al server.
} PlayerMsgType;


/**
 * GameMsgType:
 * Enumera i tipi di messaggi che possono essere inviati dal server al client.
 */
typedef enum {
    MSG_WELCOME,                    // Messaggio di benvenuto inviato dopo il login, contiene informazioni sull'utente.
    MSG_GAME_CREATED,               // Conferma della creazione di una nuova partita, con relativo ID.
    MSG_GAME_JOINED,                // Conferma dell'unione a una partita esistente.

    MSG_ERROR_CREATE_GAME,          // Errore durante la creazione della partita.
    MSG_ERROR_JOIN_GAME,            // Errore durante l'unione a una partita.
    MSG_ERROR_NOT_AUTHENTICATED,    // Errore di autenticazione.
    

    MSG_GAME_STATE_UPDATE,          // Aggiornamento generale sullo stato della partita (giocatori, flotte, ecc.).
    MSG_PLAYER_JOINED,              // Notifica che un nuovo giocatore si è unito alla partita.
    MSG_PLAYER_LEFT,                // Notifica che un giocatore ha abbandonato la partita.
    MSG_GAME_STARTED,               // Notifica che la partita è iniziata.
    // MSG_PLAYER_READY,               // Notifica che un giocatore è pronto a giocare.
    MSG_TURN_ORDER_UPDATE,          // Aggiornamento sull'ordine di turno dei giocatori.
    MSG_YOUR_TURN,                  // Notifica che è il turno del client di effettuare una mossa.
    MSG_ATTACK_UPDATE,              // Aggiornamento sul risultato di un attacco (colpito, mancato, affondato).
    MSG_GAME_FINISHED,              // Notifica che la partita è finita.

    MSG_ERROR_START_GAME,           // Errore durante l'avvio della partita.
    MSG_ERROR_PLAYER_ACTION,        // Errore durante un'azione del giocatore.
    MSG_ERROR_NOT_YOUR_TURN,        // Errore di turno non valido.

    
    MSG_ERROR_UNEXPECTED_MESSAGE,   // Messaggio inaspettato ricevuto.
    MSG_ERROR_MALFORMED_MESSAGE     // Messaggio malformato ricevuto.
} GameMsgType;


#define HEADER_SIZE sizeof(Header)

typedef struct {
    uint16_t msgType;
    uint32_t payloadSize;
} Header;

typedef struct {
    Header header;
    char *payload;
} Msg;


typedef struct _PayloadNode {
    char *key;
    char *value;
    struct _PayloadNode *next;
} PayloadNode;

typedef struct _PayloadList {
    PayloadNode *head;
    struct _PayloadList *next;
} PayloadList;

typedef struct {
    PayloadList *head;
    PayloadList *tail;
    int size;
} Payload;


Msg *recvMsg(int socket_fd);
int sendMsg(int socket_fd, Msg *msg);

Msg *createMsg(uint16_t header_type, uint32_t payload_size, char *payload);
void freeMsg(Msg *msg);

Payload *createEmptyPayload();
int addPayloadKeyValuePair(Payload *payload, const char *key, const char *value);
int addPayloadKeyValuePairInt(Payload *payload, const char *key, int value);
int addPayloadList(Payload *payload);

char *getPayloadValue(Payload *payload, int index, const char *key);
int getPayloadIntValue(Payload *payload, int index, const char *key, int *value_out);
int getPayloadListSize(Payload *payload);

Payload *parsePayload(char *buffer);
char *serializePayload(Payload *payload);

void freePayload(Payload *payload);

int safeSendMsgWithoutCleanup(int client_fd, uint16_t msg_type, Payload *payload);
int safeSendMsg(int client_fd, uint16_t msg_type, Payload *payload);
int safeRecvMsg(int client_fd, uint16_t *msg_type_out, Payload **payload_out);

#endif // PROTOCOL_H
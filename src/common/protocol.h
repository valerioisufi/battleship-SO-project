#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef enum {
    MSG_LOGIN,
    MSG_CREATE_GAME,
    MSG_JOIN_GAME,
    MSG_LEAVE_GAME,
    MSG_READY_TO_PLAY,
    MSG_START_GAME,
    MSG_ATTACK,
    MSG_SETUP_FLEET
} PlayerMsgType;

typedef enum {
    MSG_WELCOME,
    MSG_GAMES_LIST,
    MSG_GAME_CREATED,
    MSG_GAME_JOINED,
    MSG_GAME_STARTED,
    MSG_PLAYER_JOINED,
    MSG_PLAYER_READY,
    MSG_PLAYER_LEFT,
    MSG_ATTACK_UPDATE,
    MSG_GAME_STATE_UPDATE,
    MSG_YOUR_TURN,
    MSG_ERROR_CREATE_GAME,
    MSG_ERROR_JOIN_GAME,
    MSG_ERROR_START_GAME,
    MSG_ERROR_PLAYER_ACTION,
    MSG_ERROR_NOT_YOUR_TURN,
    MSG_ERROR_NOT_AUTHENTICATED,
    MSG_ERROR_UNEXPECTED_MESSAGE,
    MSG_ERROR_MALFORMED_MESSAGE
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
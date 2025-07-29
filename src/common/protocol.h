#include <stdint.h>

typedef enum {
    MSG_LOGIN,
    MSG_CREATE_GAME,
    MSG_JOIN_GAME,
    MSG_START_GAME,
    MSG_PLAYER_ACTION
} PlayerMsgType;

typedef enum {
    MSG_WELCOME,
    MSG_GAMES_LIST,
    MSG_GAME_STATE_UPDATE,
    MSG_YOUR_TURN,
    MSG_PLAYER_LEAVE,
    MSG_ERROR_UNEXPECTED_MESSAGE,
    MSG_ERROR_NOT_YOUR_TURN
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


Msg *recvMsg(int socket_fd);
void sendMsg(int socket_fd, Msg *msg);

Msg *createMsg(uint16_t header_type, uint32_t payload_size, char *payload);
void freeMsg(Msg *msg);

PayloadNode *parsePayload(char *buffer);
char *serializePayload(PayloadNode *head);

void updatePayload(PayloadNode *head, char *key, char *value);
char *getPayloadValue(PayloadNode *head, char *key);
void freePayloadNodes(PayloadNode *head);
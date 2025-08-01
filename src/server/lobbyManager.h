#include "common/protocol.h"

void *lobby_thread_main(void *arg);
void cleanup_client_lobby(int epoll_fd, int client_fd, unsigned int user_id);

void on_login_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, PayloadNode *payload);
void on_create_game_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, PayloadNode *payload);
void on_join_game_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, PayloadNode *payload);

char *require_authentication(int lobby_epoll_fd, unsigned int user_id, int client_s);

int on_malformed_msg(int lobby_epoll_fd, unsigned int user_id, int client_s);
void on_unexpected_msg(int lobby_epoll_fd, unsigned int user_id, int client_s, uint16_t msg_type);
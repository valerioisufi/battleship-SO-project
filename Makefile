CC = gcc
CFLAGS = -Wall -Wextra -pedantic -g -Isrc -DDEBUG
LDFLAGS = -lpthread
SRC_DIR = src

COMMON_SRC = $(SRC_DIR)/common/protocol.c $(SRC_DIR)/common/game.c $(SRC_DIR)/utils/list.c $(SRC_DIR)/utils/cmdLineParser.c $(SRC_DIR)/utils/userInput.c
CLIENT_SRC = $(SRC_DIR)/client/client.c $(COMMON_SRC) $(SRC_DIR)/client/clientGameManager.c $(SRC_DIR)/client/gameUI.c
SERVER_SRC = $(SRC_DIR)/server/server.c $(COMMON_SRC) $(SRC_DIR)/server/users.c $(SRC_DIR)/server/gameManager.c $(SRC_DIR)/server/lobbyManager.c

all: client server

client: $(CLIENT_SRC)
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/client $(CLIENT_SRC) $(LDFLAGS)

server: $(SERVER_SRC)
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/server $(SERVER_SRC) $(LDFLAGS)

clean:
	rm -rf bin/
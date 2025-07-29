CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc
SRC_DIR = src

CLIENT_SRC = $(SRC_DIR)/client/client.c $(SRC_DIR)/utils/cmdLineParser.c $(SRC_DIR)/common/protocol.c
SERVER_SRC = $(SRC_DIR)/server.c $(SRC_DIR)/utils/cmdLineParser.c $(SRC_DIR)/common/protocol.c

all: bin/client bin/server

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o bin/client $(CLIENT_SRC)

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -o bin/server $(SERVER_SRC)

clean:
	rm -rf bin/
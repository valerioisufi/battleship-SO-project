CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc
LDFLAGS = -lpthread
SRC_DIR = src

CLIENT_SRC = $(SRC_DIR)/client/client.c $(SRC_DIR)/utils/cmdLineParser.c $(SRC_DIR)/common/protocol.c
SERVER_SRC = $(SRC_DIR)/server/server.c $(SRC_DIR)/utils/cmdLineParser.c $(SRC_DIR)/common/protocol.c

all: client server

client: $(CLIENT_SRC)
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/client $(CLIENT_SRC) $(LDFLAGS)

server: $(SERVER_SRC)
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/server $(SERVER_SRC) $(LDFLAGS)

clean:
	rm -rf bin/
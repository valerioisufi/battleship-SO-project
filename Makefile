CC = gcc
CFLAGS = -Wall -Wextra -g
SRC_DIR = src

CLIENT_SRC = $(SRC_DIR)/client.c $(SRC_DIR)/cmdLineParser.c
SERVER_SRC = $(SRC_DIR)/server.c $(SRC_DIR)/cmdLineParser.c

all: client server

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o client $(CLIENT_SRC)

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -o server $(SERVER_SRC)

clean:
	rm -f client server
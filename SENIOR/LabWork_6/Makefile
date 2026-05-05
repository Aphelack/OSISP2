CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
TARGETS = server client

.PHONY: all clean

all: $(TARGETS)

server: server.c io.c chat_proto.h io.h
	$(CC) $(CFLAGS) -o $@ server.c io.c

client: client.c io.c chat_proto.h io.h
	$(CC) $(CFLAGS) -o $@ client.c io.c

clean:
	rm -f $(TARGETS)

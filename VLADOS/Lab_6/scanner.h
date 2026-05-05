#ifndef SCANNER_H
#define SCANNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>

#define MAX_BATCH 256
#define TIMEOUT_SEC 1

typedef struct {
    int port;
    int socket_fd;
    int is_active;
} port_target;

int set_nonblocking(int fd);

#endif
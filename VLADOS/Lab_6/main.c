#include "scanner.h"

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <IP> <Start Port> <End Port>\n", argv[0]);
        return 1;
    }

    const char *target_ip = argv[1];
    int start_port = atoi(argv[2]);
    int end_port = atoi(argv[3]);

    printf("Scanning %s from port %d to %d...\n", target_ip, start_port, end_port);

    for (int port = start_port; port <= end_port; port += MAX_BATCH) {
        port_target batch[MAX_BATCH];
        fd_set write_fds;
        FD_ZERO(&write_fds);
        int max_fd = -1;
        int current_batch_size = 0;

        for (int i = 0; i < MAX_BATCH && (port + i) <= end_port; i++) {
            int p = port + i;
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) continue;

            set_nonblocking(fd);

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(p);
            inet_pton(AF_INET, target_ip, &addr.sin_addr);

            batch[i].port = p;
            batch[i].socket_fd = fd;
            batch[i].is_active = 1;
            current_batch_size++;

            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
                if (errno != EINPROGRESS) {
                    close(fd);
                    batch[i].is_active = 0;
                    continue;
                }
            }

            FD_SET(fd, &write_fds);
            if (fd > max_fd) max_fd = fd;
        }

        struct timeval tv = {TIMEOUT_SEC, 0};
        if (select(max_fd + 1, NULL, &write_fds, NULL, &tv) > 0) {
            for (int i = 0; i < current_batch_size; i++) {
                if (!batch[i].is_active) continue;

                if (FD_ISSET(batch[i].socket_fd, &write_fds)) {
                    int error = 0;
                    socklen_t len = sizeof(error);
                    getsockopt(batch[i].socket_fd, SOL_SOCKET, SO_ERROR, &error, &len);
                    
                    if (error == 0) {
                        printf("Port %d: OPEN\n", batch[i].port);
                    }
                }
            }
        }

        for (int i = 0; i < current_batch_size; i++) {
            if (batch[i].is_active) close(batch[i].socket_fd);
        }
    }

    return 0;
}
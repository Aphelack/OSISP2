#define _POSIX_C_SOURCE 200809L

#include "chat_proto.h"
#include "io.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LINE_CAP 4096
#define RECV_CAP (CHAT_HEADER_SIZE + CHAT_MAX_BODY)

static int send_frame(int fd, chat_msg_type_t type, const void *body, uint32_t body_len) {
    uint32_t wire[4];
    chat_header_host_t h = {.magic = CHAT_MAGIC,
                            .version = CHAT_VERSION,
                            .type = (uint32_t)type,
                            .body_len = body_len};
    chat_header_to_wire(&h, wire);
    if (write_full(fd, wire, sizeof(wire)) != (ssize_t)sizeof(wire)) {
        return -1;
    }
    if (body_len > 0 && body != NULL) {
        if (write_full(fd, body, body_len) != (ssize_t)body_len) {
            return -1;
        }
    }
    return 0;
}

static void print_incoming(chat_msg_type_t type, const char *body, uint32_t body_len) {
    switch (type) {
    case CHAT_MSG_JOIN_ACK:
        printf("[server] join ok: %.*s\n", (int)body_len, body);
        break;
    case CHAT_MSG_BROADCAST:
        printf("%.*s\n", (int)body_len, body);
        break;
    case CHAT_MSG_PRIVATE:
        printf("%.*s\n", (int)body_len, body);
        break;
    case CHAT_MSG_LIST_REPLY:
        printf("[users]\n%.*s", (int)body_len, body);
        break;
    case CHAT_MSG_NOTIFY:
        printf("%.*s\n", (int)body_len, body);
        break;
    case CHAT_MSG_ERROR:
        fprintf(stderr, "[error] %.*s\n", (int)body_len, body);
        break;
    case CHAT_MSG_HISTORY:
        printf("[history] %.*s\n", (int)body_len, body);
        break;
    default:
        printf("[msg type=%u] %.*s\n", (unsigned)type, (int)body_len, body);
        break;
    }
    fflush(stdout);
}

typedef struct {
    unsigned char buf[RECV_CAP];
    size_t len;
} rx_t;

static int handshake_blocking(int sock, rx_t *rx) {
    int got_join = 0;

    for (;;) {
        while (rx->len >= CHAT_HEADER_SIZE) {
            uint32_t wh[4];
            memcpy(wh, rx->buf, CHAT_HEADER_SIZE);
            chat_header_host_t h;
            chat_header_from_wire(wh, &h);
            if (!chat_header_valid(&h)) {
                fprintf(stderr, "Protocol error, disconnecting.\n");
                return -1;
            }
            size_t need = CHAT_HEADER_SIZE + (size_t)h.body_len;
            if (rx->len < need) {
                goto need_more;
            }
            chat_msg_type_t t = (chat_msg_type_t)h.type;
            const char *body = (const char *)rx->buf + CHAT_HEADER_SIZE;
            switch (t) {
            case CHAT_MSG_ERROR:
                print_incoming(t, body, h.body_len);
                return -1;
            case CHAT_MSG_JOIN_ACK:
                print_incoming(t, body, h.body_len);
                got_join = 1;
                break;
            case CHAT_MSG_HISTORY:
                if (!got_join) {
                    fprintf(stderr, "Unexpected HISTORY before JOIN_ACK\n");
                    return -1;
                }
                print_incoming(t, body, h.body_len);
                break;
            default:
                fprintf(stderr, "Unexpected message during handshake (type=%u)\n", (unsigned)t);
                return -1;
            }
            memmove(rx->buf, rx->buf + need, rx->len - need);
            rx->len -= need;
        }
need_more:
        if (got_join && rx->len == 0) {
            return 0;
        }
        ssize_t n = recv(sock, rx->buf + rx->len, RECV_CAP - rx->len, 0);
        if (n <= 0) {
            if (n == 0) {
                fprintf(stderr, "Server closed connection during handshake.\n");
            } else {
                perror("recv");
            }
            return -1;
        }
        rx->len += (size_t)n;
    }
}

static void process_server(rx_t *rx) {
    while (rx->len >= CHAT_HEADER_SIZE) {
        uint32_t wh[4];
        memcpy(wh, rx->buf, CHAT_HEADER_SIZE);
        chat_header_host_t h;
        chat_header_from_wire(wh, &h);
        if (!chat_header_valid(&h)) {
            fprintf(stderr, "Protocol error, disconnecting.\n");
            rx->len = 0;
            return;
        }
        size_t need = CHAT_HEADER_SIZE + (size_t)h.body_len;
        if (rx->len < need) {
            break;
        }
        print_incoming((chat_msg_type_t)h.type, (const char *)rx->buf + CHAT_HEADER_SIZE, h.body_len);
        memmove(rx->buf, rx->buf + need, rx->len - need);
        rx->len -= need;
    }
}

static int incomplete_frame(const rx_t *rx) {
    if (rx->len == 0) {
        return 0;
    }
    if (rx->len < CHAT_HEADER_SIZE) {
        return 1;
    }
    uint32_t wh[4];
    memcpy(wh, rx->buf, CHAT_HEADER_SIZE);
    chat_header_host_t h;
    chat_header_from_wire(wh, &h);
    if (!chat_header_valid(&h)) {
        return 0;
    }
    return rx->len < CHAT_HEADER_SIZE + (size_t)h.body_len;
}

/* После отправки дочитываем ответ; не выходим по таймауту, пока в буфере неполный кадр. */
static void pull_server_after_send(int sock, rx_t *rx) {
    int stall = 0;
    for (int sp = 0; sp < 128; sp++) {
        int partial = incomplete_frame(rx);
        struct pollfd p = {.fd = sock, .events = POLLIN};
        int to_ms = partial ? 500 : (sp == 0 ? 200 : 50);
        if (poll(&p, 1, to_ms) <= 0) {
            if (!partial) {
                break;
            }
            if (++stall > 40) {
                break;
            }
            continue;
        }
        stall = 0;
        ssize_t n = recv(sock, rx->buf + rx->len, RECV_CAP - rx->len, 0);
        if (n <= 0) {
            break;
        }
        rx->len += (size_t)n;
        process_server(rx);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [-h host] [-p port] <nickname>\n", argv0);
    fprintf(stderr, "  Commands after connect: /list  /w <nick> <text>  /quit  /help\n");
}

static void help(void) {
    printf("Commands:\n");
    printf("  /list           — список пользователей на сервере\n");
    printf("  /w <nick> text  — личное сообщение пользователю nick\n");
    printf("  /quit           — выход\n");
    printf("  /help           — эта справка\n");
    printf("Любая другая строка — сообщение всем.\n");
}

static int connect_tcp(const char *host, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Bad host: %s\n", host);
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    return fd;
}

/* 0 — без сетевого запроса; 1 — отправлено на сервер; 2 — выход */
static int handle_user_line(int sock, char *line, size_t len) {
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    if (len == 0) {
        return 0;
    }
    if (strcmp(line, "/quit") == 0 || strcmp(line, "/q") == 0) {
        return 2;
    }
    if (strcmp(line, "/help") == 0 || strcmp(line, "/?") == 0) {
        help();
        return 0;
    }
    if (strcmp(line, "/list") == 0) {
        if (send_frame(sock, CHAT_MSG_LIST, NULL, 0) < 0) {
            perror("send");
            return 2;
        }
        return 1;
    }
    if (strncmp(line, "/w ", 3) == 0 || strncmp(line, "/msg ", 5) == 0) {
        const char *p = (strncmp(line, "/w ", 3) == 0) ? line + 3 : line + 5;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        char *space = strchr(p, ' ');
        if (space == NULL) {
            fprintf(stderr, "Usage: /w <nick> message\n");
            return 0;
        }
        *space = '\0';
        const char *target = p;
        const char *msg = space + 1;
        if (*target == '\0' || *msg == '\0') {
            fprintf(stderr, "Usage: /w <nick> message\n");
            return 0;
        }
        if (strlen(target) > CHAT_MAX_NICK) {
            fprintf(stderr, "Nickname too long\n");
            return 0;
        }
        size_t tlen = strlen(target);
        size_t mlen = strlen(msg);
        if (tlen + 1 + mlen > CHAT_MAX_BODY) {
            fprintf(stderr, "Message too long\n");
            return 0;
        }
        unsigned char body[CHAT_MAX_BODY];
        memcpy(body, target, tlen);
        body[tlen] = '\0';
        memcpy(body + tlen + 1, msg, mlen);
        uint32_t bl = (uint32_t)(tlen + 1 + mlen);
        if (send_frame(sock, CHAT_MSG_PRIVATE, body, bl) < 0) {
            perror("send");
            return 2;
        }
        return 1;
    }
    if (len > CHAT_MAX_BODY) {
        fprintf(stderr, "Message too long (max %d)\n", CHAT_MAX_BODY);
        return 0;
    }
    if (send_frame(sock, CHAT_MSG_BROADCAST, line, (uint32_t)len) < 0) {
        perror("send");
        return 2;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    uint16_t port = 7777;
    const char *nick = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "h:p:")) != -1) {
        switch (opt) {
        case 'h':
            host = optarg;
            break;
        case 'p':
            port = (uint16_t)atoi(optarg);
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }
    nick = argv[optind];
    if (strlen(nick) == 0 || strlen(nick) > CHAT_MAX_NICK) {
        fprintf(stderr, "Nickname length 1..%d\n", CHAT_MAX_NICK);
        return 1;
    }

    int sock = connect_tcp(host, port);
    if (sock < 0) {
        return 1;
    }

    if (send_frame(sock, CHAT_MSG_JOIN, nick, (uint32_t)strlen(nick)) < 0) {
        perror("send join");
        close(sock);
        return 1;
    }

    rx_t rx;
    rx.len = 0;
    if (handshake_blocking(sock, &rx) < 0) {
        close(sock);
        return 1;
    }

    printf("Connected to %s:%u as \"%s\". Type /help for commands.\n", host, (unsigned)port, nick);
    fflush(stdout);

    char linebuf[LINE_CAP];
    size_t line_pos = 0;

    for (;;) {
        struct pollfd pfd[2];
        pfd[0].fd = sock;
        pfd[0].events = POLLIN;
        pfd[1].fd = STDIN_FILENO;
        pfd[1].events = POLLIN;

        int pr = poll(pfd, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            printf("Server closed connection.\n");
            break;
        }
        if (pfd[0].revents & POLLIN) {
            ssize_t n = recv(sock, rx.buf + rx.len, RECV_CAP - rx.len, 0);
            if (n <= 0) {
                printf("Disconnected.\n");
                break;
            }
            rx.len += (size_t)n;
            process_server(&rx);
        }

        if (pfd[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            char tmp[256];
            ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("read stdin");
                break;
            }
            if (n == 0) {
                break;
            }
            for (ssize_t i = 0; i < n; i++) {
                char c = tmp[i];
                if (c == '\n') {
                    linebuf[line_pos] = '\0';
                    int ur = handle_user_line(sock, linebuf, line_pos);
                    if (ur == 2) {
                        close(sock);
                        return 0;
                    }
                    if (ur == 1) {
                        pull_server_after_send(sock, &rx);
                    }
                    line_pos = 0;
                } else {
                    if (line_pos + 1 < sizeof(linebuf)) {
                        linebuf[line_pos++] = c;
                    }
                }
            }
        }
    }

    close(sock);
    return 0;
}

#define _POSIX_C_SOURCE 200809L

#include "chat_proto.h"
#include "io.h"

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 64
#define HISTORY_LINES 32
#define RECV_CAP (CHAT_HEADER_SIZE + CHAT_MAX_BODY)

typedef struct {
    int fd;
    int used;
    int joined;
    uint32_t id;
    char nick[CHAT_MAX_NICK + 1];
    unsigned char inbuf[RECV_CAP];
    size_t inlen;
} client_t;

typedef struct {
    char line[CHAT_MAX_BODY + 1];
} hist_line_t;

static client_t clients[MAX_CLIENTS];
static uint32_t next_id = 1;
static hist_line_t history[HISTORY_LINES];
static size_t hist_count;
static size_t hist_start;

static void hist_push(const char *text) {
    size_t idx = (hist_start + hist_count) % HISTORY_LINES;
    if (hist_count < HISTORY_LINES) {
        hist_count++;
    } else {
        hist_start = (hist_start + 1) % HISTORY_LINES;
    }
    strncpy(history[idx].line, text, sizeof(history[idx].line) - 1);
    history[idx].line[sizeof(history[idx].line) - 1] = '\0';
}

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

static void close_slot(client_t *c) {
    if (c->fd >= 0) {
        close(c->fd);
    }
    memset(c, 0, sizeof(*c));
    c->fd = -1;
}

static client_t *find_by_nick(const char *nick) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].used || !clients[i].joined) {
            continue;
        }
        if (strcmp(clients[i].nick, nick) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

static int nick_taken(const char *nick, const client_t *except) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].used || !clients[i].joined) {
            continue;
        }
        if (&clients[i] == except) {
            continue;
        }
        if (strcmp(clients[i].nick, nick) == 0) {
            return 1;
        }
    }
    return 0;
}

static void broadcast_notify(const char *text, const client_t *skip) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > CHAT_MAX_BODY) {
        len = CHAT_MAX_BODY;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].used || !clients[i].joined) {
            continue;
        }
        if (skip != NULL && &clients[i] == skip) {
            continue;
        }
        if (send_frame(clients[i].fd, CHAT_MSG_NOTIFY, text, len) < 0) {
            /* ignore */
        }
    }
}

static void send_history_to(client_t *c) {
    for (size_t k = 0; k < hist_count; k++) {
        size_t idx = (hist_start + k) % HISTORY_LINES;
        const char *line = history[idx].line;
        uint32_t bl = (uint32_t)strlen(line);
        if (bl > CHAT_MAX_BODY) {
            bl = CHAT_MAX_BODY;
        }
        if (send_frame(c->fd, CHAT_MSG_HISTORY, line, bl) < 0) {
            break;
        }
    }
}

static int handle_join(client_t *c, const char *body, uint32_t body_len) {
    char nick[CHAT_MAX_NICK + 1];
    if (body_len == 0 || body_len > CHAT_MAX_NICK) {
        const char *e = "Invalid nickname length";
        send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
        return -1;
    }
    memcpy(nick, body, body_len);
    nick[body_len] = '\0';
    /* trim */
    while (body_len > 0 && (nick[body_len - 1] == '\n' || nick[body_len - 1] == '\r')) {
        nick[--body_len] = '\0';
    }
    if (body_len == 0) {
        const char *e = "Empty nickname";
        send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
        return -1;
    }
    if (nick_taken(nick, c)) {
        const char *e = "Nickname already in use";
        send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
        return -1;
    }
    strncpy(c->nick, nick, sizeof(c->nick) - 1);
    c->nick[sizeof(c->nick) - 1] = '\0';
    c->joined = 1;

    char ack[128];
    snprintf(ack, sizeof(ack), "id=%u\nnick=%s\n", c->id, c->nick);
    if (send_frame(c->fd, CHAT_MSG_JOIN_ACK, ack, (uint32_t)strlen(ack)) < 0) {
        return -1;
    }
    send_history_to(c);

    char note[CHAT_MAX_BODY + 1];
    snprintf(note, sizeof(note), "[JOIN] %s", c->nick);
    broadcast_notify(note, c);
    return 0;
}

static int handle_broadcast(client_t *c, const char *body, uint32_t body_len) {
    if (!c->joined) {
        return 0;
    }
    char line[CHAT_MAX_BODY + CHAT_MAX_NICK + 8];
    snprintf(line, sizeof(line), "%s: %.*s", c->nick, (int)body_len, body);
    hist_push(line);
    uint32_t out_len = (uint32_t)strlen(line);
    if (out_len > CHAT_MAX_BODY) {
        out_len = CHAT_MAX_BODY;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].used || !clients[i].joined) {
            continue;
        }
        if (send_frame(clients[i].fd, CHAT_MSG_BROADCAST, line, out_len) < 0) {
            /* drop */
        }
    }
    return 0;
}

static int handle_private(client_t *c, const char *body, uint32_t body_len) {
    if (!c->joined) {
        return 0;
    }
    const char *sep = memchr(body, '\0', body_len);
    if (sep == NULL) {
        const char *e = "Malformed private message (need target\\0text)";
        send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
        return 0;
    }
    size_t target_len = (size_t)(sep - (const char *)body);
    const char *msg = sep + 1;
    size_t msg_len = body_len - target_len - 1;
    if (target_len == 0 || target_len > CHAT_MAX_NICK || msg_len == 0) {
        const char *e = "Invalid private message";
        send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
        return 0;
    }
    char target[CHAT_MAX_NICK + 1];
    memcpy(target, body, target_len);
    target[target_len] = '\0';

    client_t *to = find_by_nick(target);
    if (to == NULL) {
        const char *e = "User not found";
        send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
        return 0;
    }
    char line[CHAT_MAX_BODY + CHAT_MAX_NICK * 2 + 16];
    snprintf(line, sizeof(line), "(pm %s -> %s) %.*s", c->nick, target, (int)msg_len, msg);
    uint32_t ol = (uint32_t)strlen(line);
    if (ol > CHAT_MAX_BODY) {
        ol = CHAT_MAX_BODY;
    }
    if (send_frame(to->fd, CHAT_MSG_PRIVATE, line, ol) < 0) {
        return 0;
    }
    if (to != c && send_frame(c->fd, CHAT_MSG_PRIVATE, line, ol) < 0) {
        return 0;
    }
    return 0;
}

static int handle_list(client_t *c) {
    char buf[CHAT_MAX_BODY + 1];
    size_t pos = 0;
    buf[0] = '\0';
    for (int i = 0; i < MAX_CLIENTS && pos + 1 < sizeof(buf); i++) {
        if (!clients[i].used || !clients[i].joined) {
            continue;
        }
        int n = snprintf(buf + pos, sizeof(buf) - pos, "%u\t%s\n", clients[i].id, clients[i].nick);
        if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
            break;
        }
        pos += (size_t)n;
    }
    if (pos == 0) {
        snprintf(buf, sizeof(buf), "(no users)\n");
        pos = strlen(buf);
    }
    send_frame(c->fd, CHAT_MSG_LIST_REPLY, buf, (uint32_t)pos);
    return 0;
}

static void process_message(client_t *c) {
    while (c->inlen >= CHAT_HEADER_SIZE) {
        uint32_t wh[4];
        memcpy(wh, c->inbuf, CHAT_HEADER_SIZE);
        chat_header_host_t h;
        chat_header_from_wire(wh, &h);
        if (!chat_header_valid(&h)) {
            close_slot(c);
            return;
        }
        size_t need = CHAT_HEADER_SIZE + (size_t)h.body_len;
        if (c->inlen < need) {
            break;
        }
        const char *body = (const char *)c->inbuf + CHAT_HEADER_SIZE;
        switch ((chat_msg_type_t)h.type) {
        case CHAT_MSG_JOIN:
            if (c->joined) {
                const char *e = "Already joined";
                send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
            } else {
                handle_join(c, body, h.body_len);
            }
            break;
        case CHAT_MSG_BROADCAST:
            handle_broadcast(c, body, h.body_len);
            break;
        case CHAT_MSG_PRIVATE:
            handle_private(c, body, h.body_len);
            break;
        case CHAT_MSG_LIST:
            handle_list(c);
            break;
        default:
            break;
        }
        memmove(c->inbuf, c->inbuf + need, c->inlen - need);
        c->inlen -= need;
    }
}

static int add_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].used) {
            memset(&clients[i], 0, sizeof(clients[i]));
            clients[i].fd = fd;
            clients[i].used = 1;
            clients[i].joined = 0;
            clients[i].id = next_id++;
            clients[i].inlen = 0;
            return i;
        }
    }
    return -1;
}

static void remove_client_index(int idx) {
    client_t *c = &clients[idx];
    if (c->joined) {
        char note[CHAT_MAX_BODY + 1];
        snprintf(note, sizeof(note), "[PART] %s", c->nick);
        broadcast_notify(note, c);
    }
    close_slot(c);
}

static int make_listener(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 16) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv) {
    uint16_t port = 7777;
    if (argc >= 2) {
        port = (uint16_t)atoi(argv[1]);
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    int listen_fd = make_listener(port);
    if (listen_fd < 0) {
        return 1;
    }

    printf("Chat server listening on TCP port %u\n", (unsigned)port);

    struct pollfd fds[MAX_CLIENTS + 1];
    for (;;) {
        int nfds = 1;
        fds[0].fd = listen_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].used) {
                continue;
            }
            fds[nfds].fd = clients[i].fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        int pr = poll(fds, (nfds_t)nfds, -1);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            int cfd = accept(listen_fd, (struct sockaddr *)&peer, &plen);
            if (cfd < 0) {
                if (errno != EINTR) {
                    perror("accept");
                }
            } else {
                int idx = add_client(cfd);
                if (idx < 0) {
                    const char *e = "Server full";
                    uint32_t wire[4];
                    chat_header_host_t h = {.magic = CHAT_MAGIC,
                                            .version = CHAT_VERSION,
                                            .type = CHAT_MSG_ERROR,
                                            .body_len = (uint32_t)strlen(e)};
                    chat_header_to_wire(&h, wire);
                    (void)write_full(cfd, wire, sizeof(wire));
                    (void)write_full(cfd, e, h.body_len);
                    close(cfd);
                }
            }
        }

        int fi = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].used) {
                continue;
            }
            if (fi >= nfds) {
                break;
            }
            short rev = fds[fi].revents;
            fi++;
            if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
                remove_client_index(i);
                continue;
            }
            if (rev & POLLIN) {
                ssize_t n = recv(clients[i].fd, clients[i].inbuf + clients[i].inlen,
                                 RECV_CAP - clients[i].inlen, 0);
                if (n <= 0) {
                    remove_client_index(i);
                    continue;
                }
                clients[i].inlen += (size_t)n;
                process_message(&clients[i]);
                if (!clients[i].used) {
                    continue;
                }
                if (clients[i].inlen >= RECV_CAP) {
                    remove_client_index(i);
                }
            }
        }
    }

    close(listen_fd);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].used) {
            close_slot(&clients[i]);
        }
    }
    return 0;
}

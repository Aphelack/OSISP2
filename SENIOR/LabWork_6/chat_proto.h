#ifndef CHAT_PROTO_H
#define CHAT_PROTO_H

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

#define CHAT_MAGIC 0x43484154u
#define CHAT_VERSION 1u

#define CHAT_MAX_NICK 31
#define CHAT_MAX_BODY 2048
#define CHAT_HEADER_SIZE 16

typedef enum {
    CHAT_MSG_JOIN = 1,
    CHAT_MSG_JOIN_ACK,
    CHAT_MSG_BROADCAST,
    CHAT_MSG_PRIVATE,
    CHAT_MSG_LIST,
    CHAT_MSG_LIST_REPLY,
    CHAT_MSG_NOTIFY,
    CHAT_MSG_ERROR,
    CHAT_MSG_HISTORY,
} chat_msg_type_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t body_len;
} chat_header_host_t;

static inline void chat_header_to_wire(const chat_header_host_t *h, uint32_t out[4]) {
    out[0] = htonl(h->magic);
    out[1] = htonl(h->version);
    out[2] = htonl(h->type);
    out[3] = htonl(h->body_len);
}

static inline void chat_header_from_wire(const uint32_t in[4], chat_header_host_t *h) {
    h->magic = ntohl(in[0]);
    h->version = ntohl(in[1]);
    h->type = ntohl(in[2]);
    h->body_len = ntohl(in[3]);
}

static inline int chat_header_valid(const chat_header_host_t *h) {
    if (h->magic != CHAT_MAGIC) {
        return 0;
    }
    if (h->version != CHAT_VERSION) {
        return 0;
    }
    if (h->body_len > CHAT_MAX_BODY) {
        return 0;
    }
    return 1;
}

#endif

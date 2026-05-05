# LabWork 6. TCP-чат с собственным протоколом

## Идея лабораторной

Лабораторная реализует многоклиентский чат поверх TCP. Есть сервер, который принимает подключения, хранит список клиентов и пересылает сообщения. Есть клиент, который подключается к серверу, отправляет команды пользователя и одновременно принимает входящие сообщения.

Главная особенность работы — собственный протокол сообщений. Каждое сообщение передается не просто как строка, а как кадр:

```text
header 16 bytes + body
```

Заголовок содержит magic-число, версию протокола, тип сообщения и длину тела. Это позволяет надежно отличать разные виды сообщений и понимать, сколько байтов нужно дочитать из TCP-потока.

## Как собрать и запускать

Из каталога `LabWork_6`:

```bash
make
```

Запустить сервер на порту по умолчанию `7777`:

```bash
./server
```

Или указать порт:

```bash
./server 9000
```

Подключить клиента:

```bash
./client Alice
```

Подключить клиента к конкретному хосту и порту:

```bash
./client -h 127.0.0.1 -p 9000 Bob
```

Команды клиента:

```text
/list             список пользователей
/w <nick> text    личное сообщение
/quit             выход
/help             помощь
любая другая строка отправляется всем
```

Для проверки удобно открыть несколько терминалов: в одном сервер, в остальных клиенты с разными никами.

## Протокол сообщений

Протокол описан в `chat_proto.h`:

```c
#define CHAT_MAGIC 0x43484154u
#define CHAT_VERSION 1u
#define CHAT_MAX_NICK 31
#define CHAT_MAX_BODY 2048
#define CHAT_HEADER_SIZE 16
```

`CHAT_MAGIC` — контрольное значение, по которому программа понимает, что перед ней действительно сообщение этого протокола. `CHAT_VERSION` позволяет проверять совместимость клиента и сервера. `CHAT_MAX_BODY` ограничивает размер тела сообщения.

Типы сообщений:

```c
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
```

Заголовок в памяти программы:

```c
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t body_len;
} chat_header_host_t;
```

Перед отправкой поля переводятся в сетевой порядок байтов:

```c
static inline void chat_header_to_wire(const chat_header_host_t *h, uint32_t out[4]) {
    out[0] = htonl(h->magic);
    out[1] = htonl(h->version);
    out[2] = htonl(h->type);
    out[3] = htonl(h->body_len);
}
```

При получении выполняется обратное преобразование:

```c
static inline void chat_header_from_wire(const uint32_t in[4], chat_header_host_t *h) {
    h->magic = ntohl(in[0]);
    h->version = ntohl(in[1]);
    h->type = ntohl(in[2]);
    h->body_len = ntohl(in[3]);
}
```

Проверка заголовка защищает от мусорных или слишком больших сообщений:

```c
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
```

## Надежная запись в сокет

TCP не гарантирует, что один вызов `write` отправит все байты. Поэтому в `io.c` есть `write_full`:

```c
ssize_t write_full(int fd, const void *buf, size_t count) {
    const unsigned char *p = buf;
    size_t left = count;

    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }
    return (ssize_t)count;
}
```

Функция повторяет `write`, пока не отправит весь буфер. Если системный вызов прерван сигналом (`EINTR`), попытка повторяется.

Отправка кадра на сервере и клиенте устроена одинаково:

```c
static int send_frame(int fd, chat_msg_type_t type, const void *body, uint32_t body_len) {
    uint32_t wire[4];
    chat_header_host_t h = {
        .magic = CHAT_MAGIC,
        .version = CHAT_VERSION,
        .type = (uint32_t)type,
        .body_len = body_len
    };
    chat_header_to_wire(&h, wire);
    write_full(fd, wire, sizeof(wire));
    write_full(fd, body, body_len);
    return 0;
}
```

Сначала отправляется заголовок, затем тело сообщения.

## Привязка к коду сервера

Сервер хранит клиентов в массиве:

```c
typedef struct {
    int fd;
    int used;
    int joined;
    uint32_t id;
    char nick[CHAT_MAX_NICK + 1];
    unsigned char inbuf[RECV_CAP];
    size_t inlen;
} client_t;
```

`fd` — сокет клиента, `used` показывает занятость слота, `joined` означает успешный вход в чат, `nick` хранит имя, `inbuf` и `inlen` нужны для накопления входящих байтов.

Сервер создает слушающий TCP-сокет:

```c
int fd = socket(AF_INET, SOCK_STREAM, 0);
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
bind(fd, (struct sockaddr *)&addr, sizeof(addr));
listen(fd, 16);
```

`SO_REUSEADDR` позволяет быстрее перезапускать сервер на том же порту.

Главный цикл использует `poll`:

```c
int pr = poll(fds, (nfds_t)nfds, -1);
```

`poll` позволяет одному потоку ждать события сразу на нескольких файловых дескрипторах: слушающем сокете и сокетах клиентов.

Если событие пришло на слушающий сокет, сервер принимает нового клиента:

```c
int cfd = accept(listen_fd, (struct sockaddr *)&peer, &plen);
int idx = add_client(cfd);
```

Если данные пришли от существующего клиента, сервер читает их в буфер:

```c
ssize_t n = recv(clients[i].fd, clients[i].inbuf + clients[i].inlen,
                 RECV_CAP - clients[i].inlen, 0);
clients[i].inlen += (size_t)n;
process_message(&clients[i]);
```

`process_message` разбирает накопленные байты на кадры:

```c
while (c->inlen >= CHAT_HEADER_SIZE) {
    memcpy(wh, c->inbuf, CHAT_HEADER_SIZE);
    chat_header_from_wire(wh, &h);
    if (!chat_header_valid(&h)) {
        close_slot(c);
        return;
    }
    size_t need = CHAT_HEADER_SIZE + (size_t)h.body_len;
    if (c->inlen < need) {
        break;
    }
```

Это важно, потому что TCP — поток байтов. Одно сообщение может прийти частями, а несколько сообщений могут прийти вместе. Поэтому сервер хранит буфер и обрабатывает только полные кадры.

При широковещательном сообщении сервер добавляет строку в историю и отправляет всем участникам:

```c
snprintf(line, sizeof(line), "%s: %.*s", c->nick, (int)body_len, body);
hist_push(line);

for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i].used || !clients[i].joined) {
        continue;
    }
    send_frame(clients[i].fd, CHAT_MSG_BROADCAST, line, out_len);
}
```

Личные сообщения ищут получателя по нику:

```c
client_t *to = find_by_nick(target);
if (to == NULL) {
    const char *e = "User not found";
    send_frame(c->fd, CHAT_MSG_ERROR, e, (uint32_t)strlen(e));
    return 0;
}
```

История хранится в кольцевом буфере на 32 строки:

```c
static hist_line_t history[HISTORY_LINES];
static size_t hist_count;
static size_t hist_start;
```

Когда новый клиент успешно входит, сервер отправляет ему `JOIN_ACK`, а затем историю:

```c
send_frame(c->fd, CHAT_MSG_JOIN_ACK, ack, (uint32_t)strlen(ack));
send_history_to(c);
```

## Привязка к коду клиента

Клиент подключается к TCP-серверу:

```c
int fd = socket(AF_INET, SOCK_STREAM, 0);
...
if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(fd);
    return -1;
}
```

После подключения клиент отправляет `JOIN` с ником:

```c
send_frame(sock, CHAT_MSG_JOIN, nick, (uint32_t)strlen(nick));
```

Затем он ждет подтверждение:

```c
if (handshake_blocking(sock, &rx) < 0) {
    close(sock);
    return 1;
}
```

В основном цикле клиент одновременно следит за сокетом и стандартным вводом:

```c
struct pollfd pfd[2];
pfd[0].fd = sock;
pfd[0].events = POLLIN;
pfd[1].fd = STDIN_FILENO;
pfd[1].events = POLLIN;

int pr = poll(pfd, 2, -1);
```

Если пришли данные от сервера, они читаются и разбираются:

```c
ssize_t n = recv(sock, rx.buf + rx.len, RECV_CAP - rx.len, 0);
rx.len += (size_t)n;
process_server(&rx);
```

Если пользователь ввел строку, она обрабатывается как команда:

```c
if (strcmp(line, "/list") == 0) {
    send_frame(sock, CHAT_MSG_LIST, NULL, 0);
    return 1;
}
```

Личное сообщение кодируется как `target\0message`:

```c
memcpy(body, target, tlen);
body[tlen] = '\0';
memcpy(body + tlen + 1, msg, mlen);
send_frame(sock, CHAT_MSG_PRIVATE, body, bl);
```

Обычная строка отправляется всем:

```c
send_frame(sock, CHAT_MSG_BROADCAST, line, (uint32_t)len);
```

## На что обратить внимание

Сервер однопоточный, но обслуживает много клиентов благодаря `poll`. Это хорошая модель для учебного сетевого приложения: нет гонок между потоками, но есть полноценная работа с несколькими соединениями.

TCP передает поток байтов, а не готовые сообщения. Поэтому наличие заголовка с длиной и буфера входящих данных — ключевая часть лабораторной.

Ограничения реализации: максимум 64 клиента, максимум 31 символ в нике, максимум 2048 байт в теле сообщения, история на 32 строки.

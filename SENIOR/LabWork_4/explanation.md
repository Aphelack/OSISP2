# LabWork 4. Клиент-сервер через FIFO

## Идея лабораторной

Лабораторная показывает межпроцессное взаимодействие через именованные каналы FIFO. Есть сервер, который постоянно ждет запросы, и клиенты, которые отправляют команды: `echo`, `upper`, `lower`, `length`, `reverse`.

FIFO можно воспринимать как специальный файл: один процесс пишет в него байты, другой читает. В отличие от обычного файла, данные не сохраняются на диск как содержимое, а проходят через буфер ядра.

Архитектура такая:

1. Сервер создает общий FIFO `/tmp/lab4_server_fifo`.
2. Клиент создает свой личный FIFO `/tmp/lab4_client_<PID>_fifo`.
3. Клиент отправляет запрос в общий FIFO сервера.
4. Сервер обрабатывает запрос и пишет ответ в FIFO конкретного клиента.
5. Клиент читает ответ и удаляет свой FIFO.

## Как собрать и запускать

Из каталога `LabWork_4`:

```bash
make
```

В первом терминале запустить сервер:

```bash
./server
```

Во втором терминале запустить клиента:

```bash
./client
```

Можно отправить одну команду без интерактивного режима:

```bash
./client upper "hello world"
./client reverse "Hello"
./client length "abcdef"
```

Автоматический тест:

```bash
make test
```

Остановить сервер, запущенный через `make run-server`:

```bash
make stop
```

## Общий протокол

Общие типы данных находятся в `common.h`:

```c
#define FIFO_PATH_MAX 256
#define MSG_SIZE 256

typedef enum {
    REQ_ECHO,
    REQ_UPPER,
    REQ_LOWER,
    REQ_LENGTH,
    REQ_REVERSE,
    REQ_QUIT
} request_type_t;
```

Запрос содержит PID клиента, тип операции и строку:

```c
typedef struct {
    int client_pid;
    request_type_t request_type;
    char data[MSG_SIZE];
} request_t;
```

PID нужен серверу для построения имени клиентского FIFO:

```c
snprintf(client_fifo, sizeof(client_fifo), "/tmp/lab4_client_%d_fifo", req->client_pid);
```

Ответ содержит код статуса и строку результата:

```c
typedef struct {
    int status;
    char response[MSG_SIZE];
} response_t;
```

## Привязка к коду сервера

Сервер начинает с настройки сигналов и создания FIFO:

```c
signal(SIGINT, handle_signal);
signal(SIGTERM, handle_signal);
signal(SIGPIPE, SIG_IGN);

unlink(SERVER_FIFO);

if (mkfifo(SERVER_FIFO, 0666) < 0) {
    log_message("ERROR: Cannot create server FIFO: %s", strerror(errno));
    return EXIT_FAILURE;
}
```

`unlink` удаляет старый FIFO, если он остался после прошлого запуска. `mkfifo` создает новый именованный канал. `SIGPIPE` игнорируется, чтобы сервер не завершался, если клиентский FIFO неожиданно закрылся.

Дальше сервер открывает FIFO на чтение:

```c
server_fd = open(SERVER_FIFO, O_RDONLY);
```

Это блокирующая операция: сервер может ждать, пока клиент не откроет этот FIFO на запись.

Основной цикл читает запросы:

```c
while (running) {
    bytes_read = read(server_fd, &req, sizeof(req));

    if (bytes_read == 0) {
        close(server_fd);
        server_fd = open(SERVER_FIFO, O_RDONLY);
        continue;
    }

    if (bytes_read != sizeof(req)) {
        log_message("WARNING: Incomplete request received (%zd bytes)", bytes_read);
        continue;
    }

    process_request(&req);
}
```

Если `read` возвращает `0`, это означает, что все писатели закрыли FIFO. Сервер переоткрывает канал и продолжает ждать следующих клиентов.

Обработка команды выполняется через `switch`:

```c
switch (req->request_type) {
    case REQ_ECHO:
        process_echo(req->data, resp.response);
        break;
    case REQ_UPPER:
        process_upper(req->data, resp.response);
        break;
    case REQ_LOWER:
        process_lower(req->data, resp.response);
        break;
    case REQ_LENGTH:
        process_length(req->data, resp.response);
        break;
    case REQ_REVERSE:
        process_reverse(req->data, resp.response);
        break;
}
```

Например, `upper` проходит по строке и применяет `toupper`:

```c
static void process_upper(const char *input, char *output) {
    size_t len = strlen(input);
    for (size_t i = 0; i < len && i < MSG_SIZE - 1; i++) {
        output[i] = toupper((unsigned char)input[i]);
    }
    output[len] = '\0';
}
```

После обработки сервер отправляет ответ в FIFO клиента:

```c
fd = open(client_fifo, O_WRONLY);
if (write(fd, &resp, sizeof(resp)) != sizeof(resp)) {
    log_message("ERROR: Failed to write response to PID %d", req->client_pid);
}
close(fd);
```

## Привязка к коду клиента

Клиент сначала проверяет, что серверный FIFO существует:

```c
struct stat st;
if (stat(SERVER_FIFO, &st) < 0) {
    fprintf(stderr, "ERROR: Server FIFO not found: %s\n", SERVER_FIFO);
    return EXIT_FAILURE;
}
```

Затем выбирает режим: если есть аргументы командной строки, выполняется одиночная команда, иначе интерактивный режим:

```c
if (argc > 1) {
    return single_command_mode(argc, argv);
} else {
    return interactive_mode();
}
```

В функции `send_request` клиент формирует запрос:

```c
req.client_pid = getpid();
req.request_type = type;
strncpy(req.data, data, sizeof(req.data) - 1);
req.data[sizeof(req.data) - 1] = '\0';
```

Потом создает личный FIFO:

```c
snprintf(client_fifo, sizeof(client_fifo), "/tmp/lab4_client_%d_fifo", getpid());
unlink(client_fifo);

if (mkfifo(client_fifo, 0666) < 0) {
    client_log("ERROR: Cannot create client FIFO: %s", strerror(errno));
    return -1;
}
```

Запрос отправляется в серверный FIFO:

```c
server_fd = open(SERVER_FIFO, O_WRONLY);
bytes = write(server_fd, &req, sizeof(req));
close(server_fd);
```

Ответ читается из личного FIFO:

```c
client_fd = open(client_fifo, O_RDONLY);
bytes = read(client_fd, &resp, sizeof(resp));
close(client_fd);
unlink(client_fifo);
```

## На что обратить внимание

Сервер обрабатывает запросы последовательно. Несколько клиентов могут отправлять команды, но сервер берет их одну за другой.

Запросы и ответы меньше `PIPE_BUF`, поэтому записи в FIFO атомарны: данные разных клиентов не должны перемешиваться в середине одной структуры.

Файлы FIFO создаются в `/tmp`, поэтому при аварийном завершении могут остаться старые FIFO. Для очистки есть:

```bash
make clean
rm -f /tmp/lab4_*.fifo
```

В каталоге уже есть подробные `README.md` и `DETAILED_DESCRIPTION.md`; этот файл служит более коротким объяснением с привязкой к основному коду.

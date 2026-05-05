# Лабораторная работа №4: IPC Server-Client с использованием FIFO каналов

## Содержание

1. [Введение в IPC](#1-введение-в-ipc)
2. [Теория FIFO каналов](#2-теория-fifo-каналов)
3. [Архитектура системы](#3-архитектура-системы)
4. [Детальное описание компонентов](#4-детальное-описание-компонентов)
5. [Протокол взаимодействия](#5-протокол-взаимодействия)
6. [Структуры данных](#6-структуры-данных)
7. [Реализация сервера](#7-реализация-сервера)
8. [Реализация клиента](#8-реализация-клиента)
9. [Обработка сигналов](#9-обработка-сигналов)
10. [Логирование](#10-логирование)
11. [Сборка и запуск](#11-сборка-и-запуск)
12. [Тестирование](#12-тестирование)
13. [Возможные проблемы и решения](#13-возможные-проблемы-и-решения)
14. [Глубокое погружение: внутренние механизмы](#14-глубокое-погружение-внутренние-механизмы)
15. [Тонкости и нюансы реализации](#15-тонкости-и-нюансы-реализации)
16. [Производительность и оптимизация](#16-производительность-и-оптимизация)
17. [Безопасность и защита от ошибок](#17-безопасность-и-защита-от-ошибок)
18. [Сравнение с альтернативными решениями](#18-сравнение-с-альтернативными-решениями)

---

## 1. Введение в IPC

### 1.1 Что такое IPC?

**IPC (Inter-Process Communication)** — это механизм взаимодействия между процессами в операционной системе. Поскольку каждый процесс имеет своё изолированное адресное пространство, операционная система предоставляет специальные механизмы для обмена данными между ними.

### 1.2 Основные механизмы IPC в UNIX/Linux

| Механизм | Описание | Преимущества | Недостатки |
|----------|----------|--------------|------------|
| **FIFO (Named Pipes)** | Именованные каналы | Простота использования, файловый интерфейс | Однонаправленность |
| **Pipe** | Анонимные каналы | Быстрая работа | Только для родственных процессов |
| **Message Queue** | Очереди сообщений | Асинхронность, приоритеты | Сложнее в использовании |
| **Shared Memory** | Разделяемая память | Высокая скорость | Требует синхронизации |
| **Semaphore** | Семафоры | Синхронизация процессов | Не передаёт данные |
| **Socket** | Сокеты | Сетевое взаимодействие | Избыточность для локальной связи |
| **Signal** | Сигналы | Асинхронные уведомления | Ограниченный объём данных |

### 1.3 Почему FIFO?

В данной работе выбран механизм **FIFO (именованные каналы)** по следующим причинам:

- **Простота реализации** — работает как обычный файл
- **Персистентность** — существует в файловой системе
- **Стандартизация** — POSIX-совместимый механизм
- **Достаточность** — подходит для задачи "запрос-ответ"

---

## 2. Теория FIFO каналов

### 2.1 Что такое FIFO?

**FIFO (First In, First Out)** или **именованный канал (named pipe)** — это специальный файл в файловой системе, который позволяет процессам обмениваться данными в режиме "первый вошёл — первый вышел".

### 2.2 Ключевые особенности FIFO

```
┌─────────────────────────────────────────────────────────┐
│                    FIFO КАНАЛ                           │
│                                                         │
│  Процесс A (Writer)  ───────►  [Данные]  ───────►  Процесс B (Reader)
│                                                         │
│  • Данные читаются в том же порядке, в котором записаны │
│  • Канал существует в файловой системе                  │
│  • Исчезает только при явном удалении (unlink)          │
└─────────────────────────────────────────────────────────┘
```

### 2.3 Создание FIFO

Для создания именованного канала используется системный вызов `mkfifo()`:

```c
#include <sys/stat.h>

int mkfifo(const char *pathname, mode_t mode);
```

**Параметры:**
- `pathname` — путь к файлу FIFO (обычно в `/tmp/`)
- `mode` — права доступа (например, `0666`)

**Возвращаемое значение:**
- `0` — успех
- `-1` — ошибка (устанавливается `errno`)

### 2.4 Открытие FIFO

```c
#include <fcntl.h>

int open(const char *pathname, int flags);
```

**Важные особенности:**

| Флаг | Поведение |
|------|-----------|
| `O_RDONLY` | Открыть для чтения. **Блокируется**, пока кто-то не откроет для записи |
| `O_WRONLY` | Открыть для записи. **Блокируется**, пока кто-то не откроет для чтения |
| `O_RDWR` | Открыть для чтения и записи |
| `O_NONBLOCK` | Не блокировать при открытии |

### 2.5 Чтение и запись

Операции чтения и записи выполняются как с обычными файлами:

```c
// Запись в FIFO
write(fd, buffer, size);

// Чтение из FIFO
read(fd, buffer, size);
```

**Важно:** 
- `read()` блокируется, пока не появятся данные
- `write()` блокируется, пока кто-то не прочитает данные
- При закрытии всех записывающих концов, `read()` возвращает `0` (EOF)

### 2.6 Удаление FIFO

```c
#include <unistd.h>

unlink("/path/to/fifo");  // Удаляет файл FIFO
```

---

## 3. Архитектура системы

### 3.1 Общая схема

```
                         ┌─────────────────────────────────────────┐
                         │              SERVER                     │
                         │                                         │
                         │  ┌─────────────────────────────────┐    │
                         │  │      server.fifo (общий)        │    │
                         │  │   /tmp/lab4_server_fifo         │    │
                         │  └─────────────────────────────────┘    │
                         │              ▲                          │
                         │              │ Читает запросы           │
                         │              │ Отправляет ответы        │
                         └──────────────┼──────────────────────────┘
                                        │
        ┌───────────────────────────────┼───────────────────────────────┐
        │                               │                               │
        ▼                               ▼                               ▼
┌───────────────┐             ┌───────────────┐             ┌───────────────┐
│   CLIENT 1    │             │   CLIENT 2    │             │   CLIENT N    │
│   PID: 1234   │             │   PID: 5678   │             │   PID: XXXX   │
│               │             │               │             │               │
│ client.fifo   │             │ client.fifo   │             │ client.fifo   │
│ /tmp/lab4_    │             │ /tmp/lab4_    │             │ /tmp/lab4_    │
│ client_1234_  │             │ client_5678_  │             │ client_XXXX_  │
│ fifo          │             │ fifo          │             │ fifo          │
└───────────────┘             └───────────────┘             └───────────────┘
```

### 3.2 Компоненты системы

| Компонент | Файл | Назначение |
|-----------|------|------------|
| **Сервер** | `server.c` | Принимает и обрабатывает запросы от клиентов |
| **Клиент** | `client.c` | Отправляет запросы серверу и получает ответы |
| **Общий заголовок** | `common.h` | Общие структуры, константы, определения |
| **Сборка** | `Makefile` | Автоматизация компиляции |
| **Тесты** | `test.sh` | Автоматическое тестирование |

### 3.3 Поток данных

```
┌──────────────────────────────────────────────────────────────────────┐
│                        ПОТОК ДАННЫХ                                  │
└──────────────────────────────────────────────────────────────────────┘

  КЛИЕНТ                                    СЕРВЕР
    │                                         │
    │  1. Создаёт client_PID_fifo             │
    │  ┌─────────────────────────────────┐    │
    │  │ mkfifo("/tmp/lab4_client_1234_fifo") │
    │  └─────────────────────────────────┘    │
    │                                         │
    │  2. Открывает server.fifo (O_WRONLY)    │
    │  ┌─────────────────────────────────┐    │
    │  │ open(SERVER_FIFO, O_WRONLY)    │    │
    │  └─────────────────────────────────┘    │
    │                                         │
    │  3. Отправляет запрос (request_t)       │
    │  ┌─────────────────────────────────┐    │
    │  │ write(server_fd, &req, size)   │    │
    │  └─────────────────────────────────┘    │
    │                    │                    │
    │                    ▼                    │
    │                    │ 4. Читает запрос   │
    │                    │ read(server_fd, ...)
    │                    │                    │
    │                    │ 5. Обрабатывает    │
    │                    │ (upper/lower/...)  │
    │                    │                    │
    │  6. Открывает client.fifo (O_RDONLY)    │
    │  ┌─────────────────────────────────┐    │
    │  │ open(client_fifo, O_RDONLY)    │    │
    │  └─────────────────────────────────┘    │
    │                    ▲                    │
    │                    │ 7. Отправляет ответ│
    │                    │ write(client_fd, ...)
    │                    │                    │
    │  8. Получает ответ (response_t)         │
    │  ┌─────────────────────────────────┐    │
    │  │ read(client_fd, &resp, size)   │    │
    │  └─────────────────────────────────┘    │
    │                                         │
    │  9. Удаляет client.fifo                 │
    │  ┌─────────────────────────────────┐    │
    │  │ unlink(client_fifo)            │    │
    │  └─────────────────────────────────┘    │
    │                                         │
```

---

## 4. Детальное описание компонентов

### 4.1 Файл common.h

Этот файл содержит **общие определения**, используемые и сервером, и клиентом.

```c
#ifndef COMMON_H
#define COMMON_H

#include <limits.h>

#define FIFO_PATH_MAX 256    // Максимальная длина пути к FIFO
#define MSG_SIZE 256         // Максимальный размер сообщения

// Типы запросов (перечисление)
typedef enum {
    REQ_ECHO,           // Вернуть текст обратно
    REQ_UPPER,          // Преобразовать в верхний регистр
    REQ_LOWER,          // Преобразовать в нижний регистр
    REQ_LENGTH,         // Вернуть длину строки
    REQ_REVERSE,        // Перевернуть строку
    REQ_QUIT            // Завершить работу клиента
} request_type_t;

// Структура запроса
typedef struct {
    int client_pid;              // PID клиента для ответа
    request_type_t request_type; // Тип запроса
    char data[MSG_SIZE];         // Данные запроса
} request_t;

// Структура ответа
typedef struct {
    int status;                  // 0 - успех, -1 - ошибка
    char response[MSG_SIZE];     // Данные ответа
} response_t;

// Константы путей
#define SERVER_FIFO "/tmp/lab4_server_fifo"
#define LOG_FILE "server.log"

#endif /* COMMON_H */
```

#### Почему нужен общий заголовок?

1. **Согласованность** — сервер и клиент используют одинаковые структуры
2. **Поддержка** — изменение в одном месте
3. **Безопасность типов** — компилятор проверяет соответствие

### 4.2 Структура request_t

```
┌─────────────────────────────────────────────────────────┐
│                    request_t                            │
│                                                         │
│  0               4                                   260│
│  ┌───────────────┬───────────────┬─────────────────────┐│
│  │ client_pid    │ request_type  │      data[]         ││
│  │ (4 байта)     │ (4 байта)     │    (256 байт)       ││
│  └───────────────┴───────────────┴─────────────────────┘│
│                                                         │
│  Общий размер: sizeof(int) + sizeof(enum) + 256 байт   │
│                ≈ 264 байт                               │
└─────────────────────────────────────────────────────────┘
```

**Поля:**
- `client_pid` — идентификатор процесса клиента. Нужен серверу, чтобы знать, куда отправлять ответ
- `request_type` — тип операции (echo, upper, lower, length, reverse, quit)
- `data` — строка с данными для обработки

### 4.3 Структура response_t

```
┌─────────────────────────────────────────────────────────┐
│                    response_t                           │
│                                                         │
│  0               4                                    260│
│  ┌───────────────┬─────────────────────────────────────┐│
│  │    status     │           response[]                ││
│  │  (4 байта)    │            (256 байт)               ││
│  └───────────────┴─────────────────────────────────────┘│
│                                                         │
│  Общий размер: sizeof(int) + 256 байт ≈ 260 байт       │
└─────────────────────────────────────────────────────────┘
```

**Поля:**
- `status` — код результата: `0` = успех, `-1` = ошибка
- `response` — строка с результатом обработки или сообщением об ошибке

---

## 5. Протокол взаимодействия

### 5.1 Последовательность операций

```
┌─────────────────────────────────────────────────────────────────────┐
│                    ПРОТОКОЛ ВЗАИМОДЕЙСТВИЯ                          │
└─────────────────────────────────────────────────────────────────────┘

ЭТАП 1: ИНИЦИАЛИЗАЦИЯ СЕРВЕРА
═══════════════════════════════════════════════════════════════════════
  1. Удалить старый FIFO (если существует): unlink(SERVER_FIFO)
  2. Создать новый FIFO: mkfifo(SERVER_FIFO, 0666)
  3. Открыть FIFO для чтения: open(SERVER_FIFO, O_RDONLY)
  4. Перейти в основной цикл обработки

ЭТАП 2: РАБОТА КЛИЕНТА
═══════════════════════════════════════════════════════════════════════
  1. Проверить существование SERVER_FIFO
  2. Создать персональный FIFO: mkfifo("/tmp/lab4_client_<PID>_fifo")
  3. Открыть SERVER_FIFO для записи: open(SERVER_FIFO, O_WRONLY)
  4. Сформировать запрос (request_t)
  5. Отправить запрос: write(server_fd, &req, sizeof(req))
  6. Закрыть SERVER_FIFO: close(server_fd)
  7. Открыть свой FIFO для чтения: open(client_fifo, O_RDONLY)
  8. Ждать ответ: read(client_fd, &resp, sizeof(resp))
  9. Получить ответ и обработать
  10. Удалить свой FIFO: unlink(client_fifo)

ЭТАП 3: ОБРАБОТКА НА СЕРВЕРЕ
═══════════════════════════════════════════════════════════════════════
  1. Прочитать запрос: read(server_fd, &req, sizeof(req))
  2. Определить тип запроса по req.request_type
  3. Выполнить обработку данных req.data
  4. Сформировать ответ (response_t)
  5. Открыть клиентский FIFO: open(client_fifo, O_WRONLY)
  6. Отправить ответ: write(client_fd, &resp, sizeof(resp))
  7. Закрыть клиентский FIFO: close(client_fd)

ЭТАП 4: ЗАВЕРШЕНИЕ
═══════════════════════════════════════════════════════════════════════
  При получении SIGINT/SIGTERM:
  1. Выйти из основного цикла
  2. Закрыть серверный FIFO
  3. Удалить серверный FIFO: unlink(SERVER_FIFO)
```

### 5.2 Диаграмма последовательности

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      ДИАГРАММА ПОСЛЕДОВАТЕЛЬНОСТИ                        │
└──────────────────────────────────────────────────────────────────────────┘

  КЛИЕНТ                          СЕРВЕР                      ФС (FIFO)
    │                               │                            │
    │  mkfifo client_PID_fifo       │                            │
    ├───────────────────────────────┼───────────────────────────►│
    │                               │                            │
    │  open server.fifo (write)     │                            │
    ├───────────────────────────────┼───────────────────────────►│
    │                               │                            │
    │  write(request)               │                            │
    ├───────────────────────────────┼───────────────────────────►│
    │                               │                            │
    │                               │  read(request)             │
    │                               ├───────────────────────────►│
    │                               │                            │
    │                               │  process(request)          │
    │                               │  (upper/lower/...)         │
    │                               │                            │
    │                               │  open client_FIFO (write)  │
    │                               ├───────────────────────────►│
    │                               │                            │
    │                               │  write(response)           │
    │                               ├───────────────────────────►│
    │                               │                            │
    │  open client_FIFO (read)      │                            │
    ├───────────────────────────────┼───────────────────────────►│
    │                               │                            │
    │  read(response)               │                            │
    ├───────────────────────────────┼───────────────────────────►│
    │                               │                            │
    │  unlink client_FIFO           │                            │
    ├───────────────────────────────┼───────────────────────────►│
    │                               │                            │
```

---

## 6. Структуры данных

### 6.1 request_type_t — перечисление типов запросов

```c
typedef enum {
    REQ_ECHO,      // 0 - Вернуть текст обратно
    REQ_UPPER,     // 1 - Преобразовать в верхний регистр
    REQ_LOWER,     // 2 - Преобразовать в нижний регистр
    REQ_LENGTH,    // 3 - Вернуть длину строки
    REQ_REVERSE,   // 4 - Перевернуть строку
    REQ_QUIT       // 5 - Завершить работу клиента
} request_type_t;
```

**Значения:**
| Константа | Значение | Описание |
|-----------|----------|----------|
| `REQ_ECHO` | 0 | Возвращает входную строку без изменений |
| `REQ_UPPER` | 1 | Преобразует все буквы в верхний регистр |
| `REQ_LOWER` | 2 | Преобразует все буквы в нижний регистр |
| `REQ_LENGTH` | 3 | Возвращает длину строки в формате "Length: N" |
| `REQ_REVERSE` | 4 | Разворачивает строку в обратном порядке |
| `REQ_QUIT` | 5 | Сигнал о завершении работы клиента |

### 6.2 Таблица команд клиента

| Команда | Тип запроса | Входные данные | Выходные данные |
|---------|-------------|----------------|-----------------|
| `echo <text>` | `REQ_ECHO` | `"Hello"` | `"Hello"` |
| `upper <text>` | `REQ_UPPER` | `"Hello"` | `"HELLO"` |
| `lower <text>` | `REQ_LOWER` | `"HELLO"` | `"hello"` |
| `length <text>` | `REQ_LENGTH` | `"Hello"` | `"Length: 5"` |
| `reverse <text>` | `REQ_REVERSE` | `"Hello"` | `"olleH"` |
| `quit` | `REQ_QUIT` | — | — |

---

## 7. Реализация сервера

### 7.1 Инициализация

```c
int main(void) {
    int server_fd;
    request_t req;
    ssize_t bytes_read;

    /* Установка обработчиков сигналов */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Удаляем старый FIFO если существует */
    unlink(SERVER_FIFO);

    /* Создаём серверный FIFO */
    if (mkfifo(SERVER_FIFO, 0666) < 0) {
        log_message("ERROR: Cannot create server FIFO: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    log_message("Server started, FIFO created: %s", SERVER_FIFO);

    /* Открываем FIFO для чтения */
    server_fd = open(SERVER_FIFO, O_RDONLY);
    if (server_fd < 0) {
        log_message("ERROR: Cannot open server FIFO: %s", strerror(errno));
        unlink(SERVER_FIFO);
        return EXIT_FAILURE;
    }

    log_message("Server ready to accept requests...");
    printf("Server running. Press Ctrl+C to stop.\n");
```

**Что происходит:**
1. Регистрируются обработчики сигналов `SIGINT` (Ctrl+C) и `SIGTERM`
2. Удаляется старый FIFO (защита от зависших экземпляров)
3. Создаётся новый FIFO с правами `0666` (чтение/запись для всех)
4. FIFO открывается для чтения — операция **блокируется**, пока клиент не откроет для записи

### 7.2 Основной цикл обработки

```c
while (running) {
    bytes_read = read(server_fd, &req, sizeof(req));

    if (bytes_read < 0) {
        if (errno == EINTR) {
            continue;  // Прервано сигналом, продолжаем
        }
        log_message("ERROR: Read error: %s", strerror(errno));
        break;
    }

    if (bytes_read == 0) {
        // Все клиенты закрыли запись, переза открываем
        log_message("All writers closed, reopening FIFO...");
        close(server_fd);
        server_fd = open(SERVER_FIFO, O_RDONLY);
        if (server_fd < 0) {
            log_message("ERROR: Cannot reopen server FIFO: %s", strerror(errno));
            break;
        }
        continue;
    }

    if (bytes_read != sizeof(req)) {
        log_message("WARNING: Incomplete request received (%zd bytes)", bytes_read);
        continue;
    }

    // Обрабатываем запрос
    if (process_request(&req) < 0) {
        log_message("WARNING: Failed to process request from PID %d", req.client_pid);
    }
}
```

**Логика работы:**
1. `read()` блокируется, пока не поступит запрос
2. Если `bytes_read < 0` — ошибка чтения (кроме `EINTR` — прерывание сигналом)
3. Если `bytes_read == 0` — все клиенты отключились, нужно переоткрыть FIFO
4. Если размер не совпадает — некорректный запрос, игнорируем
5. Вызывается `process_request()` для обработки

### 7.3 Обработка запросов

```c
static int process_request(const request_t *req) {
    response_t resp;
    char client_fifo[FIFO_PATH_MAX];
    int fd;

    // Формируем имя клиентского FIFO
    snprintf(client_fifo, sizeof(client_fifo), 
             "/tmp/lab4_client_%d_fifo", req->client_pid);

    log_message("Processing request from PID %d, type=%d, data=\"%s\"",
                req->client_pid, req->request_type, req->data);

    // Инициализируем ответ
    memset(&resp, 0, sizeof(resp));
    resp.status = 0;

    // Обрабатываем в зависимости от типа
    switch (req->request_type) {
        case REQ_ECHO:
            process_echo(req->data, resp.response);
            log_message("  -> ECHO: \"%s\"", resp.response);
            break;
        case REQ_UPPER:
            process_upper(req->data, resp.response);
            log_message("  -> UPPER: \"%s\"", resp.response);
            break;
        case REQ_LOWER:
            process_lower(req->data, resp.response);
            log_message("  -> LOWER: \"%s\"", resp.response);
            break;
        case REQ_LENGTH:
            process_length(req->data, resp.response);
            log_message("  -> LENGTH: %s", resp.response);
            break;
        case REQ_REVERSE:
            process_reverse(req->data, resp.response);
            log_message("  -> REVERSE: \"%s\"", resp.response);
            break;
        case REQ_QUIT:
            log_message("  -> QUIT request from PID %d", req->client_pid);
            return 0;  // Не отправляем ответ
        default:
            resp.status = -1;
            snprintf(resp.response, sizeof(resp.response), 
                     "Unknown request type: %d", req->request_type);
            log_message("  -> ERROR: %s", resp.response);
            break;
    }

    // Отправляем ответ
    fd = open(client_fifo, O_WRONLY);
    if (fd < 0) {
        log_message("ERROR: Cannot open client FIFO %s: %s", 
                    client_fifo, strerror(errno));
        return -1;
    }

    if (write(fd, &resp, sizeof(resp)) != sizeof(resp)) {
        log_message("ERROR: Failed to write response to PID %d", req->client_pid);
        close(fd);
        return -1;
    }

    close(fd);
    log_message("Response sent to PID %d", req->client_pid);
    return 0;
}
```

### 7.4 Функции обработки

#### process_echo — возврат строки

```c
static void process_echo(const char *input, char *output) {
    strncpy(output, input, MSG_SIZE - 1);
    output[MSG_SIZE - 1] = '\0';
}
```

#### process_upper — верхний регистр

```c
static void process_upper(const char *input, char *output) {
    size_t len = strlen(input);
    for (size_t i = 0; i < len && i < MSG_SIZE - 1; i++) {
        output[i] = toupper((unsigned char)input[i]);
    }
    output[len] = '\0';
}
```

**Важно:** `(unsigned char)` нужен для корректной обработки расширенной ASCII

#### process_lower — нижний регистр

```c
static void process_lower(const char *input, char *output) {
    size_t len = strlen(input);
    for (size_t i = 0; i < len && i < MSG_SIZE - 1; i++) {
        output[i] = tolower((unsigned char)input[i]);
    }
    output[len] = '\0';
}
```

#### process_length — длина строки

```c
static void process_length(const char *input, char *output) {
    snprintf(output, MSG_SIZE, "Length: %zu", strlen(input));
}
```

#### process_reverse — разворот строки

```c
static void process_reverse(const char *input, char *output) {
    size_t len = strlen(input);
    for (size_t i = 0; i < len && i < MSG_SIZE - 1; i++) {
        output[i] = input[len - 1 - i];
    }
    output[len] = '\0';
}
```

**Алгоритм:**
```
Вход: "Hello" (len=5)
  i=0: output[0] = input[4] = 'o'
  i=1: output[1] = input[3] = 'l'
  i=2: output[2] = input[2] = 'l'
  i=3: output[3] = input[1] = 'e'
  i=4: output[4] = input[0] = 'H'
Выход: "olleH"
```

### 7.5 Завершение работы

```c
// Завершение работы
log_message("Server shutting down...");
close(server_fd);
unlink(SERVER_FIFO);
log_message("Server stopped.");

return EXIT_SUCCESS;
```

---

## 8. Реализация клиента

### 8.1 Режимы работы

Клиент поддерживает **два режима**:

1. **Интерактивный режим** — запуск без аргументов
2. **Одиночная команда** — запуск с аргументами командной строки

```c
int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Проверка существования сервера
    struct stat st;
    if (stat(SERVER_FIFO, &st) < 0) {
        fprintf(stderr, "ERROR: Server FIFO not found: %s\n", SERVER_FIFO);
        fprintf(stderr, "Make sure the server is running.\n");
        return EXIT_FAILURE;
    }

    // Выбор режима
    if (argc > 1) {
        return single_command_mode(argc, argv);  // Одиночная команда
    } else {
        return interactive_mode();                // Интерактивный режим
    }
}
```

### 8.2 Интерактивный режим

```c
static int interactive_mode(void) {
    char input[512];
    request_type_t type;
    char data[MSG_SIZE];
    char response[MSG_SIZE];

    printf("\n=== Client %d connected ===\n", getpid());
    show_help();

    while (running) {
        printf("\n> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        input[strcspn(input, "\n")] = '\0';  // Удалить newline

        if (strlen(input) == 0) {
            continue;
        }

        if (parse_command(input, &type, data) < 0) {
            if (type == REQ_QUIT) {
                break;
            }
            continue;
        }

        if (send_request(type, data, response) < 0) {
            printf("ERROR: Failed to communicate with server\n");
            continue;
        }

        printf("Response: %s\n", response);
    }

    return 0;
}
```

### 8.3 Парсинг команд

```c
static int parse_command(const char *input, request_type_t *type, char *data) {
    char cmd[32];
    const char *text_start;

    // Извлекаем команду (первое слово)
    if (sscanf(input, "%31s", cmd) != 1) {
        return -1;
    }

    // Находим начало текста после команды
    text_start = input + strlen(cmd);
    while (*text_start == ' ') text_start++;

    // Определяем тип запроса
    if (strcmp(cmd, "echo") == 0) {
        *type = REQ_ECHO;
    } else if (strcmp(cmd, "upper") == 0) {
        *type = REQ_UPPER;
    } else if (strcmp(cmd, "lower") == 0) {
        *type = REQ_LOWER;
    } else if (strcmp(cmd, "length") == 0) {
        *type = REQ_LENGTH;
    } else if (strcmp(cmd, "reverse") == 0) {
        *type = REQ_REVERSE;
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        *type = REQ_QUIT;
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        show_help();
        return -1;
    } else {
        printf("Unknown command: '%s'. Type 'help' for available commands.\n", cmd);
        return -1;
    }

    // Копируем данные
    if (*type == REQ_QUIT) {
        data[0] = '\0';
    } else {
        strncpy(data, text_start, MSG_SIZE - 1);
        data[MSG_SIZE - 1] = '\0';

        // Если данных нет, используем значение по умолчанию
        if (strlen(data) == 0 && *type != REQ_QUIT) {
            strcpy(data, "Hello, Server!");
        }
    }

    return 0;
}
```

### 8.4 Отправка запроса

```c
static int send_request(request_type_t type, const char *data, char *response) {
    request_t req;
    response_t resp;
    char client_fifo[FIFO_PATH_MAX];
    int server_fd, client_fd;
    ssize_t bytes;

    // 1. Формируем запрос
    req.client_pid = getpid();
    req.request_type = type;
    strncpy(req.data, data, sizeof(req.data) - 1);
    req.data[sizeof(req.data) - 1] = '\0';

    // 2. Создаём персональный FIFO для ответа
    snprintf(client_fifo, sizeof(client_fifo), 
             "/tmp/lab4_client_%d_fifo", getpid());
    unlink(client_fifo);  // Удаляем старый если есть

    if (mkfifo(client_fifo, 0666) < 0) {
        client_log("ERROR: Cannot create client FIFO: %s", strerror(errno));
        return -1;
    }

    client_log("Sending request: type=%d, data=\"%s\"", type, data);

    // 3. Открываем серверный FIFO и отправляем запрос
    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd < 0) {
        client_log("ERROR: Cannot open server FIFO: %s", strerror(errno));
        unlink(client_fifo);
        return -1;
    }

    bytes = write(server_fd, &req, sizeof(req));
    close(server_fd);  // Закрываем сразу после записи

    if (bytes != sizeof(req)) {
        client_log("ERROR: Failed to send request");
        unlink(client_fifo);
        return -1;
    }

    client_log("Request sent, waiting for response...");

    // 4. Открываем свой FIFO для чтения ответа
    client_fd = open(client_fifo, O_RDONLY);
    if (client_fd < 0) {
        client_log("ERROR: Cannot open client FIFO for reading: %s", strerror(errno));
        unlink(client_fifo);
        return -1;
    }

    // 5. Читаем ответ
    bytes = read(client_fd, &resp, sizeof(resp));
    close(client_fd);
    unlink(client_fifo);  // Удаляем FIFO

    if (bytes != sizeof(resp)) {
        client_log("ERROR: Failed to receive response");
        return -1;
    }

    if (resp.status != 0) {
        client_log("Server returned error: %s", resp.response);
        return -1;
    }

    strncpy(response, resp.response, MSG_SIZE - 1);
    response[MSG_SIZE - 1] = '\0';

    client_log("Response received: \"%s\"", response);
    return 0;
}
```

### 8.5 Одиночная команда

```c
static int single_command_mode(int argc, char *argv[]) {
    request_type_t type;
    char data[MSG_SIZE];
    char response[MSG_SIZE];
    char input[512];

    // Объединяем аргументы в одну строку
    input[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(input, " ");
        strcat(input, argv[i]);
    }

    if (parse_command(input, &type, data) < 0) {
        return EXIT_FAILURE;
    }

    if (send_request(type, data, response) < 0) {
        return EXIT_FAILURE;
    }

    printf("%s\n", response);
    return EXIT_SUCCESS;
}
```

**Пример использования:**
```bash
./client upper "Hello World"
# Вывод: HELLO WORLD

./client reverse "Hello"
# Вывод: olleH
```

---

## 9. Обработка сигналов

### 9.1 Что такое сигналы?

**Сигналы** — это механизм уведомления процесса о событиях. В данной работе обрабатываются:

| Сигнал | Описание | Когда возникает |
|--------|----------|-----------------|
| `SIGINT` | Прерывание от пользователя | Нажатие Ctrl+C |
| `SIGTERM` | Требование завершения | Команда `kill` |

### 9.2 Реализация обработчика

```c
/* Флаг для корректного завершения работы */
static volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
    (void)sig;  // Подавляем предупреждение о неиспользуемом параметре
    running = 0;
}
```

**Почему `volatile sig_atomic_t`?**
- `volatile` — гарантирует, что значение читается из памяти каждый раз
- `sig_atomic_t` — атомарный тип для безопасной работы в обработчиках сигналов

### 9.3 Регистрация обработчиков

```c
signal(SIGINT, handle_signal);
signal(SIGTERM, handle_signal);
signal(SIGPIPE, SIG_IGN);  // Игнорируем SIGPIPE при записи в закрытый FIFO
```

**Почему SIGPIPE игнорируется?**

Когда клиент закрывает свой FIFO до того, как сервер успевает отправить ответ, запись вызывает `SIGPIPE`. По умолчанию этот сигнал завершает процесс. Игнорирование позволяет серверу продолжить работу.

### 9.4 Проверка флага в цикле

```c
while (running) {
    // Основная логика
}
// Выход при running = 0
```

### 9.5 Обработка EINTR

```c
// Чтение из FIFO
bytes_read = read(server_fd, &req, sizeof(req));

if (bytes_read < 0) {
    if (errno == EINTR) {
        continue;  // Прервано сигналом, повторяем
    }
    // Другая ошибка
}

// Повторное открытие FIFO (в цикле)
while (running) {
    server_fd = open(SERVER_FIFO, O_RDONLY);
    if (server_fd >= 0) {
        break;  // Успешно
    }
    if (errno == EINTR) {
        continue;  // Прервано сигналом, пробуем снова
    }
    log_message("ERROR: Cannot reopen FIFO");
    break;
}
```

**EINTR** — ошибка "прерванный системный вызов". Возникает, когда сигнал приходит во время блокирующей операции (`open`, `read`, `write`).

### 9.6 Таблица сигналов

| Сигнал | Обработчик | Действие |
|--------|------------|----------|
| `SIGINT` | `handle_signal` | Устанавливает `running = 0` |
| `SIGTERM` | `handle_signal` | Устанавливает `running = 0` |
| `SIGPIPE` | `SIG_IGN` | Игнорируется (не завершает сервер) |

---

## 10. Логирование

### 10.1 Формат логов

Сервер записывает логи в файл `server.log`:

```
[2026-03-31 12:34:56] Server started, FIFO created: /tmp/lab4_server_fifo
[2026-03-31 12:34:57] Processing request from PID 1234, type=1, data="hello"
[2026-03-31 12:34:57]   -> UPPER: "HELLO"
[2026-03-31 12:34:57] Response sent to PID 1234
```

### 10.2 Реализация логирования

```c
/* Получить текущую временную метку */
static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Логирование с временной меткой */
static void log_message(const char *format, ...) {
    va_list args;
    char timestamp[32];
    FILE *log = fopen(LOG_FILE, "a");  // Открыть для добавления

    get_timestamp(timestamp, sizeof(timestamp));

    if (log) {
        fprintf(log, "[%s] ", timestamp);
        va_start(args, format);
        vfprintf(log, format, args);  // Форматированный вывод
        va_end(args);
        fprintf(log, "\n");
        fclose(log);
    }

    // Также выводим в stderr для отладки
    fprintf(stderr, "[%s] ", timestamp);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}
```

### 10.3 Логирование в клиенте

Клиент также имеет логирование, но только в stderr:

```c
static void client_log(const char *format, ...) {
    va_list args;
    char timestamp[32];

    get_timestamp(timestamp, sizeof(timestamp));

    fprintf(stderr, "[%s] [Client %d] ", timestamp, getpid());
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}
```

---

## 11. Сборка и запуск

### 11.1 Makefile

```makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
TARGETS = server client

.PHONY: all clean test run-server stop

all: $(TARGETS)

server: server.c common.h
	$(CC) $(CFLAGS) -o $@ server.c

client: client.c common.h
	$(CC) $(CFLAGS) -o $@ client.c

clean:
	rm -f $(TARGETS) $(LOG_FILE)
	rm -f /tmp/lab4_*.fifo
	@echo "Cleaned up."

run-server: server
	@echo "Starting server..."
	./$(SERVER) &
	@sleep 1
	@echo "Server started!"

stop:
	@pkill -f "./$(SERVER)" 2>/dev/null || true
	@rm -f /tmp/lab4_server_fifo
	@echo "Server stopped."
```

### 11.2 Команды сборки

```bash
# Собрать всё
make

# Собрать только сервер
make server

# Собрать только клиент
make client

# Очистить
make clean

# Запустить сервер в фоне
make run-server

# Остановить сервер
make stop
```

### 11.3 Ручной запуск

**Терминал 1 — Сервер:**
```bash
./server
# Server running. Press Ctrl+C to stop.
```

**Терминал 2 — Клиент (интерактивный):**
```bash
./client

=== Client 1234 connected ===

Available commands:
  echo <text>     - Echo the text back
  upper <text>    - Convert text to UPPERCASE
  lower <text>    - Convert text to lowercase
  length <text>   - Get length of text
  reverse <text>  - Reverse the text
  quit            - Exit client
  help            - Show this help

> upper hello world
Response: HELLO WORLD

> reverse Hello
Response: olleH

> quit
```

**Терминал 2 — Клиент (одиночная команда):**
```bash
./client echo "Hello World"
# Hello World

./client length "Hello"
# Length: 5
```

---

## 12. Тестирование

### 12.2 Автоматический тест (Makefile)

```bash
make test
```

**Что проверяется:**
1. Очистка старых файлов
2. Запуск сервера
3. Тестирование всех команд (echo, upper, lower, length, reverse)
4. Множественные параллельные клиенты
5. Проверка логирования
6. Корректная остановка

### 12.3 Скрипт test.sh

```bash
./test.sh
```

Более подробный тест с цветным выводом и проверкой результатов.

### 12.4 Пример вывода теста

```
========================================
=== LabWork 4: IPC Server-Client Test ==
========================================

[1/6] Cleanup...
      Done.

[2/6] Starting server in background...
      Server started with PID 12345

[3/6] Testing single commands...
      Test ECHO:
Hello World
      Test UPPER:
HELLO WORLD
      Test LOWER:
hello world
      Test LENGTH:
Length: 5
      Test REVERSE:
olleH

[4/6] Testing multiple concurrent clients...
Request from client 1
Request from client 2
Request from client 3
Request from client 4
      All concurrent requests completed.

[5/6] Server log:
----------------------------------------
[2026-03-31 12:34:56] Server started, FIFO created: /tmp/lab4_server_fifo
[2026-03-31 12:34:57] Processing request from PID 12345, type=0, data="Hello World"
...
----------------------------------------

[6/6] Stopping server...
      Server stopped.

========================================
=== All tests completed successfully! ==
========================================
```

---

## 13. Возможные проблемы и решения

### 13.1 Сервер не запускается

**Проблема:**
```
ERROR: Cannot create server FIFO: File exists
```

**Решение:**
```bash
rm -f /tmp/lab4_server_fifo
make clean
make
```

### 13.2 Клиент не может подключиться

**Проблема:**
```
ERROR: Server FIFO not found: /tmp/lab4_server_fifo
```

**Решение:**
```bash
# Убедиться, что сервер запущен
ps aux | grep server

# Запустить сервер
./server &
```

### 13.3 Зависший сервер

**Проблема:** Сервер не реагирует на Ctrl+C

**Решение:**
```bash
# Найти PID
pgrep -f "./server"

# Убить принудительно
pkill -9 -f "./server"

# Очистить FIFO
rm -f /tmp/lab4_server_fifo
```

### 13.4 Ошибка "Broken pipe"

**Проблема:** Клиент получает ошибку при записи

**Причина:** Сервер закрылся, пока клиент пытался писать

**Решение:** Перезапустить сервер, затем клиента

### 13.5 Проблемы с правами доступа

**Проблема:**
```
ERROR: Cannot open server FIFO: Permission denied
```

**Решение:**
```bash
# Проверить права
ls -l /tmp/lab4_server_fifo

# Исправить (если нужно)
chmod 666 /tmp/lab4_server_fifo
```

### 13.6 Таблица ошибок

| Ошибка | Причина | Решение |
|--------|---------|---------|
| `File exists` | FIFO уже существует | `rm -f /tmp/lab4_*.fifo` |
| `No such file` | Сервер не запущен | Запустить сервер |
| `Permission denied` | Нет прав доступа | Проверить `chmod` |
| `Broken pipe` | Сервер закрылся | Перезапустить сервер |
| `Resource temporarily unavailable` | Блокировка | Проверить процессы |

---

## 14. Заключение

### 14.1 Что было изучено

В данной лабораторной работе были рассмотрены:

1. **Механизм IPC FIFO** — именованные каналы для межпроцессного взаимодействия
2. **Архитектура клиент-сервер** — классическая модель распределённой системы
3. **Обработка сигналов** — корректное завершение работы
4. **Многопоточность** — обработка множественных клиентов
5. **Логирование** — отладка и мониторинг системы
6. **Работа с файловой системой** — создание и управление специальными файлами

### 14.2 Ключевые концепции

- **FIFO** — однонаправленный канал, существующий как файл в ФС
- **Блокирующие операции** — `open()`, `read()`, `write()` могут блокироваться
- **Персональные FIFO** — каждый клиент создаёт свой канал для ответов
- **Обработка сигналов** — `volatile sig_atomic_t` для безопасного завершения

### 14.3 Расширение функциональности

Возможные улучшения:

1. **Поддержка бинарных данных** — передача не только строк
2. **Таймауты** — защита от зависаний
3. **Асинхронная обработка** — использование `select()`/`poll()`
4. **Безопасность** — проверка прав доступа к FIFO
5. **Расширенные команды** — математические операции, работа с файлами

---

## 14. Глубокое погружение: внутренние механизмы

### 14.1 Как работает FIFO на уровне ядра

#### 14.1.1 Внутренняя структура FIFO

```
┌─────────────────────────────────────────────────────────────────────┐
│                    ЯДРО LINUX / UNIX                                │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    VFS (Virtual File System)                │   │
│  │                                                             │   │
│  │  ┌──────────────────────────────────────────────────────┐  │   │
│  │  │              inode FIFO                              │  │   │
│  │  │  ┌────────────────────────────────────────────────┐  │  │   │
│  │  │  │  mode: S_IFIFO | 0666                          │  │  │   │
│  │  │  │  uid: 1000                                      │  │  │   │
│  │  │  │  gid: 1000                                      │  │  │   │
│  │  │  │  size: 0 (всегда!)                              │  │  │   │
│  │  │  │  i_pipe: ─────────────────────────────────────┼──┼──┼──►│
│  │  │  └────────────────────────────────────────────────┘  │  │   │
│  │  └──────────────────────────────────────────────────────┘  │   │
│  │                                                             │   │
│  │                          │                                  │   │
│  │                          ▼                                  │   │
│  │  ┌──────────────────────────────────────────────────────┐  │   │
│  │  │              struct pipe_inode_info                  │  │   │
│  │  │  ┌────────────────────────────────────────────────┐  │  │   │
│  │  │  │  nrbufs: количество буферов                    │  │  │   │
│  │  │  │  curbuf: текущий буфер                         │  │  │   │
│  │  │  │  bufs[PIPE_BUFFERS]: кольцевой буфер           │  │  │   │
│  │  │  │  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┐  │  │  │   │
│  │  │  │  │ buf │ buf │ buf │ buf │ buf │ buf │ buf │  │  │  │   │
│  │  │  │  │  0  │  1  │  2  │  3  │  4  │  5  │  6  │  │  │  │   │
│  │  │  │  └─────┴─────┴─────┴─────┴─────┴─────┴─────┘  │  │  │   │
│  │  │  │  readers: список ожидающих читателей           │  │  │   │
│  │  │  │  writers: список ожидающих писателей           │  │  │   │
│  │  │  └────────────────────────────────────────────────┘  │  │   │
│  │  └──────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

#### 14.1.2 Кольцевой буфер FIFO

FIFO использует **кольцевой буфер** в памяти ядра:

```
┌─────────────────────────────────────────────────────────────────┐
│                    КОЛЬЦЕВОЙ БУФЕР                              │
│                                                                 │
│         ┌─────────────────────────────────────────┐            │
│         │  [data] [data] [free] [free] [data]     │            │
│         │     ▲                              ▲    │            │
│         │     │                              │    │            │
│         │   read                         write   │            │
│         │   pointer                        pointer│            │
│         └─────────────────────────────────────────┘            │
│                                                                 │
│  • Размер буфера по умолчанию: 64 KB (PIPE_BUF = 4096 байт)    │
│  • При заполнении writer блокируется                           │
│  • При опустошении reader блокируется                          │
│  • Указатели циклически перемещаются                           │
└─────────────────────────────────────────────────────────────────┘
```

#### 14.1.3 Системные вызовы FIFO

```
┌──────────────────────────────────────────────────────────────────┐
│                    ПОТОК ВЫЗОВОВ                                 │
└──────────────────────────────────────────────────────────────────┘

mkfifo("/tmp/fifo", 0666)
    │
    ▼
sys_mkdirat(AT_FDCWD, "/tmp/fifo", S_IFIFO | 0666)
    │
    ▼
vfs_mkdir(dir, "fifo", mode)
    │
    ▼
inode->i_mode = S_IFIFO | mode
init_special_inode(inode, mode, 0)  // Создаётся pipe_inode_info
    │
    ▼
FIFO создан в VFS, но не в inode->i_data

open("/tmp/fifo", O_WRONLY)
    │
    ▼
sys_openat(AT_FDCWD, "/tmp/fifo", O_WRONLY)
    │
    ▼
vfs_open(path, file, O_WRONLY)
    │
    ▼
fifo_open(inode, file, O_WRONLY)
    │
    ├─► Проверяет: есть ли читатель?
    │   ├─► ДА: открываем немедленно
    │   └─► НЕТ: блокируем процесс (TASK_INTERRUPTIBLE)
    │       └─► add_wait_queue(&pipe->wait)
    │       └─► schedule()
    │
    ▼
file->private_data = pipe_inode_info

write(fd, buf, count)
    │
    ▼
sys_write(fd, buf, count)
    │
    ▼
vfs_write(file, buf, count)
    │
    ▼
fifo_write(file, buf, count, pos)
    │
    ├─► Копирование данных из user space в kernel space
    │   └─► copy_from_user(kernel_buf, user_buf, count)
    │
    ├─► Блокировка mutex(&pipe->mutex)
    │
    ├─► Проверка места в буфере
    │   ├─► Есть место: копируем в кольцевой буфер
    │   └─► Нет места: блокируем (TASK_INTERRUPTIBLE)
    │
    ├─► Пробуждение читателей
    │   └─► wake_up_interruptible(&pipe->wait)
    │
    └─► Разблокировка mutex(&pipe->mutex)

read(fd, buf, count)
    │
    ▼
sys_read(fd, buf, count)
    │
    ▼
vfs_read(file, buf, count)
    │
    ▼
fifo_read(file, buf, count, pos)
    │
    ├─► Блокировка mutex(&pipe->mutex)
    │
    ├─► Проверка данных
    │   ├─► Есть данные: копируем из кольцевого буфера
    │   └─► Нет данных: блокируем (TASK_INTERRUPTIBLE)
    │
    ├─► Копирование данных в user space
    │   └─► copy_to_user(user_buf, kernel_buf, count)
    │
    ├─► Пробуждение писателей
    │   └─► wake_up_interruptible(&pipe->wait)
    │
    └─► Разблокировка mutex(&pipe->mutex)
```

### 14.2 Детали работы с памятью

#### 14.2.1 User Space vs Kernel Space

```
┌──────────────────────────────────────────────────────────────────┐
│                    РАЗДЕЛЕНИЕ ПАМЯТИ                             │
└──────────────────────────────────────────────────────────────────┘

  USER SPACE (3 GB)                    KERNEL SPACE (1 GB)
  ┌─────────────────────┐             ┌─────────────────────┐
  │                     │             │                     │
  │  Stack              │             │  Kernel Stack       │
  │  ─────────────────  │             │  ─────────────────  │
  │  Heap               │             │  Kernel Heap        │
  │  ─────────────────  │             │  ─────────────────  │
  │  BSS                │             │  Direct Memory Map  │
  │  ─────────────────  │             │  ─────────────────  │
  │  Data               │             │  Kernel Code        │
  │  ─────────────────  │             │                     │
  │  Text (code)        │             │                     │
  │                     │             │                     │
  └─────────────────────┘             └─────────────────────┘
          │                                     │
          │         ┌─────────────────┐         │
          └────────►│  copy_from_user │◄────────┘
                    │  copy_to_user   │
                    └─────────────────┘

При записи в FIFO:
  1. Данные в user space (буфер клиента)
  2. copy_from_user() → kernel space (кольцевой буфер FIFO)
  3. Сервер читает из kernel space
  4. copy_to_user() → user space (буфер сервера)
```

#### 14.2.2 Почему copy_from_user необходим?

```c
// НЕПРАВИЛЬНО (может вызвать panic ядра):
kernel_buffer = user_buffer;  // ОШИБКА!

// ПРАВИЛЬНО:
if (copy_from_user(kernel_buffer, user_buffer, size)) {
    return -EFAULT;  // Ошибка доступа к памяти пользователя
}
```

**Причины:**
1. **Виртуальная память** — адрес user_buffer не существует в kernel space
2. **Защита** — пользовательская память может быть выгружена на диск (swap)
3. **Проверка прав** — ядро должно убедиться, что адрес валиден

### 14.3 Контекст переключения процессов

```
┌──────────────────────────────────────────────────────────────────┐
│                    ПЕРЕКЛЮЧЕНИЕ КОНТЕКСТА                        │
└──────────────────────────────────────────────────────────────────┘

КЛИЕНТ (PID 1234)                          СЕРВЕР (PID 5678)
     │                                          │
     │  write(server_fd, &req, size)            │
     │  ┌────────────────────────────────┐      │
     │  │ 1. Сохранение контекста        │      │
     │  │    (регистры, PC, SP)          │      │
     │  │ 2. Копирование данных в ядро   │      │
     │  │ 3. Запись в кольцевой буфер    │      │
     │  │ 4. wake_up_interruptible()     │      │
     │  │ 5. Восстановление контекста    │      │
     │  └────────────────────────────────┘      │
     │                                          │
     │                                          │  read() возвращает
     │                                          │  ┌────────────────┐
     │                                          │  │ 1. Сохранение  │
     │                                          │  │ 2. Чтение данных│
     │                                          │  │ 3. Копирование │
     │                                          │  │ 4. Восстановление│
     │                                          │  └────────────────┘
     │                                          │
     │                                          ▼
     │                                   process_request()
     │                                          │
     │                                          │  write(client_fd, &resp)
     │                                          │  ┌────────────────┐
     │                                          │  │ Копирование    │
     │                                          │  │ wake_up()      │
     │                                          │  └────────────────┘
     │                                          │
     │  read(client_fd, &resp) ◄────────────────┤
     │  ┌────────────────────────────────┐      │
     │  │ 1. Блокировка если нет данных  │      │
     │  │ 2. Получение данных из ядра    │      │
     │  │ 3. Копирование в user space    │      │
     │  └────────────────────────────────┘      │
     │                                          │
```

### 14.4 Состояния процессов

```
┌──────────────────────────────────────────────────────────────────┐
│                    ДИАГРАММА СОСТОЯНИЙ                           │
└──────────────────────────────────────────────────────────────────┘

                    ┌─────────────────┐
                    │   TASK_RUNNING  │
                    │   (исполнение)  │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
    ┌────────────────┐ ┌────────────────┐ ┌────────────────┐
    │ TASK_INTERRUPTIBLE │ │ TASK_UNINTERRUPTIBLE │ │   TASK_STOPPED   │
    │ (прерываемое   │ │ (непрерываемое │ │   (остановлен  │
    │  ожидание)     │ │  ожидание)     │ │    отладкой)   │
    └───────┬────────┘ └────────────────┘ └────────────────┘
            │
            │  signal / wake_up()
            │
            ▼
    ┌────────────────┐
    │  TASK_RUNNING  │
    │  (готов к работе)│
    └────────────────┘

В нашей реализации:
  • read() на пустом FIFO → TASK_INTERRUPTIBLE
  • write() на полном FIFO → TASK_INTERRUPTIBLE
  • open(O_WRONLY) без читателя → TASK_INTERRUPTIBLE
  • open(O_RDONLY) без писателя → TASK_INTERRUPTIBLE
  • Обработчик сигнала → прерывает ожидание, errno = EINTR
```

---

## 15. Тонкости и нюансы реализации

### 15.1 Критические секции и гонки данных

#### 15.1.1 Потенциальная гонка при создании FIFO

```c
// ПРОБЛЕМА: гонка между проверкой и созданием
if (access(client_fifo, F_OK) != 0) {  // Проверка
    mkfifo(client_fifo, 0666);          // Создание
}
// Между проверкой и созданием другой процесс может создать FIFO!

// РЕШЕНИЕ: использовать O_EXCL с mkfifo невозможно
// Но можно обработать ошибку EEXIST:
if (mkfifo(client_fifo, 0666) < 0) {
    if (errno != EEXIST) {
        // Действительная ошибка
        return -1;
    }
    // FIFO уже существует — это нормально
}
```

#### 15.1.2 Атомарность операций

```
┌──────────────────────────────────────────────────────────────────┐
│                    АТОМАРНОСТЬ В FIFO                            │
└──────────────────────────────────────────────────────────────────┘

PIPE_BUF = 4096 байт (на Linux)

• Запись <= PIPE_BUF байт: АТОМАРНА
  └─► Данные не перемешиваются с данными других процессов

• Запись > PIPE_BUF байт: НЕ атомарна
  └─► Может перемешаться с данными других писателей

В НАШЕМ СЛУЧАЕ:
  sizeof(request_t) = 4 + 4 + 256 = 264 байта < PIPE_BUF
  sizeof(response_t) = 4 + 256 = 260 байт < PIPE_BUF
  
  ✓ Наши записи атомарны!
  ✓ Данные разных клиентов не перемешаются
```

### 15.2 Обработка граничных условий

#### 15.2.1 Пустая строка

```c
// Клиент отправляет пустую строку
./client upper ""

// Что происходит:
1. req.data = "" (пустая строка, strlen = 0)
2. process_upper("", output):
   - len = 0
   - цикл не выполняется
   - output[0] = '\0'
3. Сервер возвращает пустую строку

// Это корректное поведение!
```

#### 15.2.2 Строка максимальной длины

```c
// MSG_SIZE = 256
// Максимальная строка: 255 символов + '\0'

char data[MSG_SIZE];  // 256 байт

// Если клиент вводит 300 символов:
strncpy(req.data, input, sizeof(req.data) - 1);  // 255
req.data[sizeof(req.data) - 1] = '\0';           // Гарантируем '\0'

// Результат: строка обрезается до 255 символов
```

#### 15.2.3 Специальные символы

```c
// Табуляция, новая строка, управляющие символы
input = "Hello\tWorld\n"

// Обработка:
// • toupper()/tolower() игнорируют непечатные символы
// • strlen() считает все символы включая '\n'
// • reverse() переворачивает всё включая '\n'

// Результат reverse("Hello\tWorld\n"):
// "\ndlroW\tolleH"  // '\n' теперь в начале!
```

### 15.3 Тонкости работы с сигналами

#### 15.3.1 Почему не signal(), а sigaction()?

```c
// В коде используется signal():
signal(SIGINT, handle_signal);

// ПРОБЛЕМА:
// • signal() имеет неопределённое поведение между UNIX системами
// • На некоторых системах сбрасывается после первого сигнала
// • Нельзя установить флаги (SA_RESTART, SA_NODEFER)

// БОЛЕЕ НАДЁЖНО: sigaction()
struct sigaction sa;
sa.sa_handler = handle_signal;
sa.sa_flags = SA_RESTART;  // Автоматический перезапуск системных вызовов
sigemptyset(&sa.sa_mask);
sigaction(SIGINT, &sa, NULL);

// SA_RESTART важен:
// • Без него read() может вернуть EINTR после сигнала
// • С ним read() автоматически перезапускается
```

#### 15.3.2 Сигнально-безопасные функции

```c
// В обработчике сигнала МОЖНО использовать:
✓ running = 0;              // Присваивание
✓ _exit();                  // Завершение
✓ write();                  // Запись (для отладки)

// В обработчике сигнала НЕЛЬЗЯ использовать:
✗ printf();                 // Не безопасно!
✗ fprintf();                // Не безопасно!
✗ malloc();                 // Не безопасно!
✗ strlen();                 // Не безопасно!
✗ log_message();            // Использует fprintf, malloc!

// В НАШЕМ КОДЕ:
void handle_signal(int sig) {
    (void)sig;
    running = 0;  // ✓ Безопасно: простое присваивание
}

// log_message() вызывается в основном цикле, НЕ в обработчике
```

### 15.4 Управление файловыми дескрипторами

#### 15.4.1 Утечки файловых дескрипторов

```c
// ПРАВИЛЬНО в send_request():
server_fd = open(SERVER_FIFO, O_WRONLY);
if (server_fd < 0) {
    unlink(client_fifo);
    return -1;
}

bytes = write(server_fd, &req, sizeof(req));
close(server_fd);  // ✓ Закрываем сразу после записи

// Если забыть close():
// • Лимит дескрипторов (ulimit -n) будет исчерпан
// • Сервер не сможет открыть новые клиентские FIFO
// • Процесс "повиснет" в состоянии ожидания
```

#### 15.4.2 Наследование дескрипторов

```c
// При fork() дочерний процесс наследует все дескрипторы
// При exec() наследуются дескрипторы с FD_CLOEXEC = 0

// Рекомендуется устанавливать FD_CLOEXEC:
int fd = open(path, O_RDONLY | O_CLOEXEC);
// или
int fd = open(path, O_RDONLY);
fcntl(fd, F_SETFD, FD_CLOEXEC);

// В нашем коде это не критично, т.к. fork/exec не используются
```

### 15.5 Тонкости работы с errno

#### 15.5.1 Сохранение errno

```c
// НЕПРАВИЛЬНО:
if (open(fifo, O_WRONLY) < 0) {
    log_message("Error: %s", strerror(errno));  // errno может измениться!
    close(fd);
    return -1;
}

// ПРАВИЛЬНО:
if (open(fifo, O_WRONLY) < 0) {
    int saved_errno = errno;  // Сохраняем сразу
    log_message("Error: %s", strerror(saved_errno));
    return -1;
}

// В НАШЕМ КОДЕ:
// log_message() использует fopen/fprintf/fclose
// Эти функции могут изменить errno!
// Но в данном случае это не критично, т.к. errno не используется после
```

#### 15.5.2 Значения errno для FIFO

| errno | Когда возникает |
|-------|-----------------|
| `EACCES` | Нет прав доступа к FIFO |
| `EEXIST` | FIFO уже существует (при создании) |
| `ENXIO` | Open O_WRONLY, но нет читателя (с O_NONBLOCK) |
| `ENODEV` | Путь не существует |
| `EROFS` | Файловая система только для чтения |
| `EINTR` | Прервано сигналом во время open/read/write |
| `EAGAIN` | Нет данных для read (с O_NONBLOCK) |
| `EPIPE` | Запись в FIFO, где нет читателя (SIGPIPE) |

### 15.6 Обработка SIGPIPE

```c
// Когда возникает SIGPIPE:
// 1. Клиент создал FIFO, открыл для чтения
// 2. Сервер пытается записать
// 3. Клиент закрывает FIFO ДО записи сервера
// 4. Сервер получает SIGPIPE

// По умолчанию SIGPIPE завершает процесс!

// РЕШЕНИЕ 1: Игнорировать SIGPIPE
signal(SIGPIPE, SIG_IGN);
// write() вернёт -1 с errno = EPIPE

// РЕШЕНИЕ 2: Обрабатывать SIGPIPE
void handle_pipe(int sig) {
    log_message("Client disconnected unexpectedly");
}
signal(SIGPIPE, handle_pipe);

// В НАШЕМ КОДЕ:
// SIGPIPE не обрабатывается явно
// Но это допустимо, т.к. клиент держит FIFO открытым до получения ответа
```

---

## 16. Производительность и оптимизация

### 16.1 Анализ производительности

#### 16.1.1 Задержки в системе

```
┌──────────────────────────────────────────────────────────────────┐
│                    КОМПОНЕНТЫ ЗАДЕРЖКИ                           │
└──────────────────────────────────────────────────────────────────┘

Общая задержка = T_client_prepare + T_kernel_copy + T_server_process + T_response

1. T_client_prepare (~10-100 мкс)
   • Парсинг команды
   • Формирование request_t
   • Создание FIFO (mkfifo)

2. T_kernel_copy (~1-10 мкс)
   • copy_from_user (клиент → ядро)
   • copy_to_user (ядро → сервер)

3. T_server_process (~10-50 мкс)
   • Чтение из FIFO
   • Обработка запроса (toupper/tolower/etc)
   • Запись ответа

4. T_response (~10-100 мкс)
   • Открытие клиентского FIFO
   • Запись ответа
   • Чтение ответа клиентом

ИТОГО: ~50-300 мкс на запрос
```

#### 16.1.2 Измерение производительности

```bash
# Время выполнения одиночного запроса
time ./client upper "Hello World"

# Пропускная способность (запросов в секунду)
start=$(date +%s.%N)
for i in {1..1000}; do
    ./client echo "test" > /dev/null
done
end=$(date +%s.%N)
echo "scale=2; 1000 / ($end - $start)" | bc
# Результат: ~50-100 запросов/сек
```

### 16.2 Узкие места

#### 16.2.1 Создание/удаление FIFO

```c
// КАЖДАЯ операция send_request():
mkfifo(client_fifo, 0666);    // ~1-5 мс
// ...
unlink(client_fifo);          // ~0.5-1 мс

// Это ДОРОГО!
// При 100 запросах: 100 × (1 + 1) = 200 мс только на FIFO ops

// ОПТИМИЗАЦИЯ: переиспользовать FIFO
// Создать один FIFO при старте клиента
// Удалять только при выходе
```

#### 16.2.2 Открытие/закрытие FIFO

```c
// КАЖДАЯ операция:
open(SERVER_FIFO, O_WRONLY);  // Блокирующее, ~0.1-1 мс
write(...);
close(...);

open(client_fifo, O_RDONLY);  // Блокирующее, ~0.1-1 мс
read(...);
close(...);

// ОПТИМИЗАЦИЯ: держать дескрипторы открытыми
// Но это сложно для stateless клиента
```

#### 16.2.3 Последовательная обработка на сервере

```c
// СЕРВЕР обрабатывает ПОСЛЕДОВАТЕЛЬНО:
while (running) {
    read(...);           // Блокируется на одном клиенте
    process_request();   // Обрабатывает
    write(...);          // Отправляет ответ
    // Только потом следующий клиент!
}

// ПРОБЛЕМА: если клиент 1 медленный, клиенты 2-N ждут

// ОПТИМИЗАЦИЯ: многопоточность или select/poll
pthread_create() для каждого клиента
// или
select() для мультиплексирования
```

### 16.3 Сравнение с альтернативами

#### 16.3.1 Производительность различных IPC

| Механизм | Задержка | Пропускная способность |
|----------|----------|------------------------|
| **FIFO** | ~100 мкс | ~100 запросов/сек |
| **Unix Domain Socket** | ~50 мкс | ~200 запросов/сек |
| **Shared Memory** | ~10 мкс | ~1000 запросов/сек |
| **TCP/IP (localhost)** | ~200 мкс | ~50 запросов/сек |

#### 16.3.2 Когда использовать FIFO

```
┌──────────────────────────────────────────────────────────────────┐
│                    ВЫБОР МЕХАНИЗМА IPC                           │
└──────────────────────────────────────────────────────────────────┘

FIFO:
  ✓ Простая реализация
  ✓ Файловый интерфейс
  ✓ Достаточно для < 100 запросов/сек
  ✗ Не подходит для высокой нагрузки

Unix Domain Socket:
  ✓ Быстрее FIFO
  ✓ Поддержка select/poll
  ✓ Двусторонняя связь
  ✗ Сложнее в использовании

Shared Memory:
  ✓ Максимальная скорость
  ✓ Нет копирования через ядро
  ✗ Требует синхронизации (semaphore/mutex)
  ✗ Сложная реализация

Message Queue:
  ✓ Асинхронность
  ✓ Приоритеты сообщений
  ✗ Ограниченный размер очереди
```

### 16.4 Рекомендации по оптимизации

#### 16.4.1 Для сервера

```c
// 1. Использовать select/poll для множественных клиентов
fd_set readfds;
FD_ZERO(&readfds);
FD_SET(server_fd, &readfds);
select(max_fd + 1, &readfds, NULL, NULL, NULL);

// 2. Многопоточность
pthread_t threads[MAX_CLIENTS];
pthread_create(&thread, NULL, client_handler, (void*)client_fd);

// 3. Пул потоков
// Создаём N потоков заранее
// Запросы распределяются между свободными потоками
```

#### 16.4.2 Для клиента

```c
// 1. Переиспользование FIFO
static char client_fifo[FIFO_PATH_MAX];
static int client_fd = -1;

if (client_fd < 0) {
    snprintf(client_fifo, sizeof(client_fifo), "/tmp/lab4_client_%d_fifo", getpid());
    mkfifo(client_fifo, 0666);
    client_fd = open(client_fifo, O_RDONLY);
}
// Не закрывать до выхода

// 2. Кэширование server_fd
// Держать дескриптор открытым при частых запросах
```

---

## 17. Безопасность и защита от ошибок

### 17.1 Уязвимости безопасности

#### 17.1.1 Symlink атака

```bash
# АТАКА:
# 1. Злоумышленник создаёт symlink:
ln -s /etc/passwd /tmp/lab4_server_fifo

# 2. Запускает сервер от root
sudo ./server

# 3. Сервер удаляет /etc/passwd через unlink(SERVER_FIFO)!

// ЗАЩИТА:
// 1. Проверка перед созданием:
struct stat st;
if (lstat(SERVER_FIFO, &st) == 0) {
    if (S_ISLNK(st.st_mode)) {
        fprintf(stderr, "SECURITY: FIFO is a symlink!\n");
        return EXIT_FAILURE;
    }
}

// 2. Использовать mkfifo() с O_EXCL (невозможно для FIFO)
// 3. Создавать в защищённой директории:
mkdir -p /var/run/lab4
chmod 700 /var/run/lab4
SERVER_FIFO = "/var/run/lab4/server_fifo"
```

#### 17.1.2 Race condition при создании

```bash
# АТАКА:
# 1. Клиент проверяет: test -f /tmp/lab4_client_1234_fifo  # false
# 2. Злоумышленник создаёт FIFO: mkfifo /tmp/lab4_client_1234_fifo
# 3. Клиент открывает для чтения
# 4. Злоумышленник читает данные!

// ЗАЩИТА:
// 1. Использовать уникальные имена:
snprintf(client_fifo, sizeof(client_fifo), 
         "/tmp/lab4_client_%d_%ld_fifo", 
         getpid(), random());  // Добавить случайное число

// 2. Проверка владельца FIFO:
fstat(fd, &st);
if (st.st_uid != getuid()) {
    // FIFO создан не нами!
}
```

#### 17.1.3 Переполнение буфера

```c
// В НАШЕМ КОДЕ защита есть:
strncpy(req.data, data, sizeof(req.data) - 1);
req.data[sizeof(req.data) - 1] = '\0';  // Гарантируем null-терминатор

// Но strncpy имеет проблемы:
// • Не гарантирует '\0' если src >= n
// • Медленная (заполняет нулями до n)

// ЛУЧШЕ:
snprintf(req.data, sizeof(req.data), "%s", data);
// или
strlcpy(req.data, data, sizeof(req.data));  // Если доступна
```

### 17.2 Защита от DoS

#### 17.2.1 Flood запросов

```bash
# АТАКА:
for i in {1..10000}; do
    ./client echo "flood" &
done

// Сервер захлёбывается от запросов

// ЗАЩИТА:
// 1. Ограничение частоты запросов от одного PID
typedef struct {
    pid_t pid;
    time_t last_request;
    int request_count;
} client_stats_t;

client_stats_t clients[MAX_CLIENTS];

if (now - client->last_request < 1) {  // Меньше 1 секунды
    client->request_count++;
    if (client->request_count > MAX_REQUESTS_PER_SEC) {
        log_message("RATE LIMIT: PID %d exceeded", req->client_pid);
        continue;  // Игнорируем запрос
    }
}
```

#### 17.2.2 Поддельный PID

```bash
# АТАКА:
// Злоумышленник может отправить запрос с чужим PID
request_t fake_req;
fake_req.client_pid = 1;  // PID init/systemd!
fake_req.request_type = REQ_ECHO;
strcpy(fake_req.data, "test");

// Сервер попытается отправить ответ в несуществующий FIFO
// или в FIFO системы

// ЗАЩИТА:
// 1. Проверка существования процесса:
if (kill(req->client_pid, 0) < 0 && errno == ESRCH) {
    log_message("INVALID PID: %d does not exist", req->client_pid);
    return -1;
}

// 2. Проверка владельца процесса:
struct stat st;
if (stat(client_fifo, &st) == 0) {
    if (st.st_uid != getuid()) {
        log_message("SECURITY: FIFO owned by different user");
        return -1;
    }
}
```

### 17.3 Обработка некорректных данных

#### 17.3.1 Бинарные данные в строке

```c
// АТАКА:
request_t req;
req.data[0] = '\x00';  // Null byte в начале
req.data[1] = 'A';
req.data[2] = '\x00';

// strlen(req.data) = 0!
// process_upper() ничего не сделает

// ЗАЩИТА:
// 1. Передавать длину данных отдельно
typedef struct {
    int client_pid;
    request_type_t request_type;
    size_t data_len;      // Явная длина
    char data[MSG_SIZE];
} request_t;

// 2. Валидация данных:
for (size_t i = 0; i < strlen(req.data); i++) {
    if (!isprint((unsigned char)req.data[i]) && 
        !isspace((unsigned char)req.data[i])) {
        log_message("INVALID DATA: non-printable character at position %zu", i);
        return -1;
    }
}
```

#### 17.3.2 Очень длинные команды

```c
// АТАКА:
./client upper "$(python3 -c 'print("A" * 10000)')"

// ЗАЩИТА:
// 1. Ограничение на входе:
if (strlen(input) > MAX_INPUT_SIZE) {
    printf("ERROR: Input too long (max %d characters)\n", MAX_INPUT_SIZE);
    return -1;
}

// 2. Обрезка в parse_command():
strncpy(data, text_start, MSG_SIZE - 1);
data[MSG_SIZE - 1] = '\0';
```

### 17.4 Контроль целостности

#### 17.4.1 Проверка контрольной суммы

```c
// Для критичных данных добавить CRC32:
typedef struct {
    int client_pid;
    request_type_t request_type;
    uint32_t checksum;    // CRC32
    char data[MSG_SIZE];
} request_t;

uint32_t calculate_crc32(const void *data, size_t len) {
    // Реализация CRC32
}

// При отправке:
req.checksum = calculate_crc32(&req, offsetof(request_t, checksum));

// При получении:
if (req.checksum != calculate_crc32(&req, offsetof(request_t, checksum))) {
    log_message("CORRUPTED DATA: checksum mismatch");
    return -1;
}
```

---

## 18. Сравнение с альтернативными решениями

### 18.1 Unix Domain Sockets

#### 18.1.1 Реализация на сокетах

```c
// СЕРВЕР на Unix Domain Sockets:
int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

struct sockaddr_un addr;
addr.sun_family = AF_UNIX;
strcpy(addr.sun_path, "/tmp/lab4_server.sock");

bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
listen(server_fd, 5);

while (1) {
    int client_fd = accept(server_fd, NULL, NULL);
    read(client_fd, &req, sizeof(req));
    process_request(&req);
    write(client_fd, &resp, sizeof(resp));
    close(client_fd);
}

// КЛИЕНТ:
int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));
write(client_fd, &req, sizeof(req));
read(client_fd, &resp, sizeof(resp));
close(client_fd);
```

#### 18.1.2 Сравнение FIFO vs Sockets

| Характеристика | FIFO | Unix Socket |
|----------------|------|-------------|
| **Сложность** | Простой | Средний |
| **Производительность** | Средняя | Высокая |
| **Двусторонняя связь** | Нет (нужно 2 FIFO) | Да (один сокет) |
| **select/poll** | Ограниченно | Полная поддержка |
| **Неблокирующий режим** | Да | Да |
| **Права доступа** | Через файл | Через socket options |
| **Персистентность** | Да (файл) | Нет (исчезает) |

### 18.2 Message Queues (POSIX)

#### 18.2.1 Реализация на очередях

```c
// СЕРВЕР:
mqd_t mq = mq_open("/lab4_queue", O_CREAT | O_RDONLY, 0666, &attr);

struct mq_attr attr = {
    .mq_flags = 0,
    .mq_maxmsg = 10,
    .mq_msgsize = sizeof(request_t)
};

while (1) {
    unsigned int prio;
    mq_receive(mq, (char*)&req, sizeof(req), &prio);
    process_request(&req);
}

// КЛИЕНТ:
mqd_t mq = mq_open("/lab4_queue", O_WRONLY);
mq_send(mq, (char*)&req, sizeof(req), 0);
```

#### 18.2.2 Сравнение FIFO vs Message Queues

| Характеристика | FIFO | Message Queue |
|----------------|------|---------------|
| **Асинхронность** | Нет | Да (mq_notify) |
| **Приоритеты** | Нет | Да |
| **Размер сообщения** | Ограничен буфером | Настраиваемый |
| **Персистентность** | Да | Нет (исчезает после close) |
| **Портативность** | Высокая | POSIX, нет в старых UNIX |

### 18.3 Shared Memory

#### 18.3.1 Реализация на разделяемой памяти

```c
// ОБЩАЯ СТРУКТУРА:
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond_request;
    pthread_cond_t cond_response;
    request_t request;
    response_t response;
    int request_ready;
    int response_ready;
} shared_data_t;

// СЕРВЕР:
int shm_fd = shm_open("/lab4_shm", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(shared_data_t));
shared_data_t *data = mmap(NULL, sizeof(shared_data_t), 
                           PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

while (1) {
    pthread_mutex_lock(&data->mutex);
    while (!data->request_ready)
        pthread_cond_wait(&data->cond_request, &data->mutex);
    
    process_request(&data->request, &data->response);
    
    data->response_ready = 1;
    data->request_ready = 0;
    pthread_cond_signal(&data->cond_response);
    pthread_mutex_unlock(&data->mutex);
}
```

#### 18.3.2 Сравнение FIFO vs Shared Memory

| Характеристика | FIFO | Shared Memory |
|----------------|------|---------------|
| **Производительность** | Средняя | Максимальная |
| **Сложность** | Низкая | Высокая |
| **Синхронизация** | Встроенная | Ручная (mutex/semaphore) |
| **Копирование данных** | Через ядро | Нет (прямой доступ) |
| **Масштабируемость** | Ограничена | Высокая |

### 18.4 Итоговая таблица выбора IPC

```
┌──────────────────────────────────────────────────────────────────┐
│                    ВЫБОР IPC МЕХАНИЗМА                           │
└──────────────────────────────────────────────────────────────────┘

Задача                          │ Рекомендуемый IPC
────────────────────────────────┼─────────────────────────────────
Простая связь процесс-процесс   │ FIFO
Высокая производительность      │ Shared Memory + Semaphore
Сетевое взаимодействие          │ TCP/IP Sockets
Локальное клиент-сервер         │ Unix Domain Sockets
Асинхронные уведомления         │ Message Queue + Signal
Синхронизация без данных        │ Semaphore
Общий доступ к данным           │ Shared Memory
```

### 18.5 Почему FIFO для данной работы?

```
┌──────────────────────────────────────────────────────────────────┐
│                    ОБОСНОВАНИЕ ВЫБОРА FIFO                       │
└──────────────────────────────────────────────────────────────────┘

✓ ПРОСТОТА:
  • Минимальный код для понимания студентами
  • Файловый интерфейс (open/read/write/close)
  • Не требует сложной синхронизации

✓ ОБУЧАЮЩАЯ ЦЕННОСТЬ:
  • Демонстрирует базовые принципы IPC
  • Показывает работу с файловыми дескрипторами
  • Иллюстрирует блокирующие операции

✓ ДОСТАТОЧНОСТЬ:
  • Производительность достаточна для лабораторной
  • Поддерживает множественных клиентов
  • Реализует полный цикл "запрос-ответ"

✓ ПОРТАТИВНОСТЬ:
  • Работает на всех UNIX-системах
  • Не требует дополнительных библиотек
  • POSIX-совместимый код

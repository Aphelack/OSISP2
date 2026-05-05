#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>

#include "common.h"

/* Флаг для корректного завершения работы */
static volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

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
    FILE *log = fopen(LOG_FILE, "a");
    
    get_timestamp(timestamp, sizeof(timestamp));
    
    if (log) {
        fprintf(log, "[%s] ", timestamp);
        va_start(args, format);
        vfprintf(log, format, args);
        va_end(args);
        fprintf(log, "\n");
        fclose(log);
    }
    
    /* Также выводим в stderr для отладки */
    fprintf(stderr, "[%s] ", timestamp);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* Обработка запроса ECHO - вернуть текст обратно */
static void process_echo(const char *input, char *output) {
    strncpy(output, input, MSG_SIZE - 1);
    output[MSG_SIZE - 1] = '\0';
}

/* Обработка запроса UPPER - преобразовать в верхний регистр */
static void process_upper(const char *input, char *output) {
    size_t len = strlen(input);
    for (size_t i = 0; i < len && i < MSG_SIZE - 1; i++) {
        output[i] = toupper((unsigned char)input[i]);
    }
    output[len] = '\0';
}

/* Обработка запроса LOWER - преобразовать в нижний регистр */
static void process_lower(const char *input, char *output) {
    size_t len = strlen(input);
    for (size_t i = 0; i < len && i < MSG_SIZE - 1; i++) {
        output[i] = tolower((unsigned char)input[i]);
    }
    output[len] = '\0';
}

/* Обработка запроса LENGTH - вернуть длину строки */
static void process_length(const char *input, char *output) {
    snprintf(output, MSG_SIZE, "Length: %zu", strlen(input));
}

/* Обработка запроса REVERSE - перевернуть строку */
static void process_reverse(const char *input, char *output) {
    size_t len = strlen(input);
    for (size_t i = 0; i < len && i < MSG_SIZE - 1; i++) {
        output[i] = input[len - 1 - i];
    }
    output[len] = '\0';
}

/* Обработать запрос и отправить ответ */
static int process_request(const request_t *req) {
    response_t resp;
    char client_fifo[FIFO_PATH_MAX];
    int fd;
    
    /* Формируем имя персонального FIFO клиента */
    snprintf(client_fifo, sizeof(client_fifo), "/tmp/lab4_client_%d_fifo", req->client_pid);
    
    log_message("Processing request from PID %d, type=%d, data=\"%s\"", 
                req->client_pid, req->request_type, req->data);
    
    /* Инициализируем ответ */
    memset(&resp, 0, sizeof(resp));
    resp.status = 0;
    
    /* Обрабатываем запрос в зависимости от типа */
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
            return 0; /* Не отправляем ответ, просто завершаем */
        default:
            resp.status = -1;
            snprintf(resp.response, sizeof(resp.response), "Unknown request type: %d", req->request_type);
            log_message("  -> ERROR: %s", resp.response);
            break;
    }
    
    /* Отправляем ответ через персональный FIFO клиента */
    fd = open(client_fifo, O_WRONLY);
    if (fd < 0) {
        log_message("ERROR: Cannot open client FIFO %s: %s", client_fifo, strerror(errno));
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

int main(void) {
    int server_fd;
    request_t req;
    ssize_t bytes_read;
    
    /* Установка обработчиков сигналов для корректного завершения */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);  /* Игнорируем SIGPIPE при записи в закрытый клиентский FIFO */

    /* Удаляем старый FIFO если существует */
    unlink(SERVER_FIFO);

    /* Создаём серверный FIFO */
    if (mkfifo(SERVER_FIFO, 0666) < 0) {
        log_message("ERROR: Cannot create server FIFO: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    log_message("Server started, FIFO created: %s", SERVER_FIFO);

    /* Открываем FIFO для чтения (блокирующее открытие) */
    server_fd = open(SERVER_FIFO, O_RDONLY);
    if (server_fd < 0) {
        if (errno == EINTR && !running) {
            /* Прервано сигналом до начала работы */
            log_message("Server startup interrupted.");
            unlink(SERVER_FIFO);
            return EXIT_SUCCESS;
        }
        log_message("ERROR: Cannot open server FIFO: %s", strerror(errno));
        unlink(SERVER_FIFO);
        return EXIT_FAILURE;
    }
    
    log_message("Server ready to accept requests...");
    printf("Server running. Press Ctrl+C to stop.\n");
    
    /* Основной цикл обработки запросов */
    while (running) {
        bytes_read = read(server_fd, &req, sizeof(req));
        
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue; /* Прервано сигналом, продолжаем */
            }
            log_message("ERROR: Read error: %s", strerror(errno));
            break;
        }
        
        if (bytes_read == 0) {
            /* Все клиенты закрыли запись, переза открываем */
            log_message("All writers closed, reopening FIFO...");
            close(server_fd);
            /* Повторяем открытие пока не получим дескриптор или сигнал завершения */
            while (running) {
                server_fd = open(SERVER_FIFO, O_RDONLY);
                if (server_fd >= 0) {
                    break;  /* Успешно открыто */
                }
                if (errno == EINTR) {
                    continue;  /* Прервано сигналом, пробуем снова */
                }
                log_message("ERROR: Cannot reopen server FIFO: %s", strerror(errno));
                break;
            }
            if (server_fd < 0) {
                break;  /* Выход из основного цикла */
            }
            continue;
        }
        
        if (bytes_read != sizeof(req)) {
            log_message("WARNING: Incomplete request received (%zd bytes)", bytes_read);
            continue;
        }
        
        /* Обрабатываем запрос */
        if (process_request(&req) < 0) {
            log_message("WARNING: Failed to process request from PID %d", req.client_pid);
        }
    }
    
    /* Завершение работы */
    log_message("Server shutting down...");
    close(server_fd);
    unlink(SERVER_FIFO);
    log_message("Server stopped.");
    
    return EXIT_SUCCESS;
}

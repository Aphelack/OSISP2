#ifndef COMMON_H
#define COMMON_H

#include <limits.h>

#define FIFO_PATH_MAX 256

#define MSG_SIZE 256

typedef enum {
    REQ_ECHO,           /* Вернуть текст обратно */
    REQ_UPPER,          /* Преобразовать в верхний регистр */
    REQ_LOWER,          /* Преобразовать в нижний регистр */
    REQ_LENGTH,         /* Вернуть длину строки */
    REQ_REVERSE,        /* Перевернуть строку */
    REQ_QUIT            /* Завершить работу клиента */
} request_type_t;

typedef struct {
    int client_pid;                 /* PID клиента для ответа */
    request_type_t request_type;    /* Тип запроса */
    char data[MSG_SIZE];            /* Данные запроса */
} request_t;

typedef struct {
    int status;                     /* 0 - успех, -1 - ошибка */
    char response[MSG_SIZE];        /* Данные ответа */
} response_t;

#define SERVER_FIFO "/tmp/lab4_server_fifo"
#define LOG_FILE "server.log"

#endif /* COMMON_H */

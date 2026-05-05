#define _XOPEN_SOURCE 600
#ifndef COMMON_H
#define COMMON_H
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define NUM_WORKERS 4
#define FAILURE_CHANCE 20 

typedef struct {
    int id;
    pthread_t thread;
    volatile bool is_active;
    time_t last_heartbeat;
} worker_info_t;

extern pthread_mutex_t log_mutex;
extern volatile bool keep_running;

#endif
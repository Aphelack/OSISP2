#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "common.h"
#include "worker.h"

void* worker_routine(void* arg) {
    worker_info_t* info = (worker_info_t*)arg;


    pthread_mutex_lock(&log_mutex);
    printf("[WORKER %d]: Online and ready.\n", info->id);
    pthread_mutex_unlock(&log_mutex);

    while (keep_running) {
        
        for(int i = 0; i < 10 && keep_running; i++) {
            usleep(100000); 
        }
        
        info->last_heartbeat = time(NULL);

        if ((rand() % 100) < FAILURE_CHANCE) {
            pthread_mutex_lock(&log_mutex);
            printf("[WORKER %d]:CRITICAL FAILURE\n", info->id);
            pthread_mutex_unlock(&log_mutex);
            
            info->is_active = false; 
            pthread_exit(NULL);      
        }

        pthread_mutex_lock(&log_mutex);
        printf("[WORKER %d]: Task completed successfully.\n", info->id);
        pthread_mutex_unlock(&log_mutex);
    }

    return NULL;
}
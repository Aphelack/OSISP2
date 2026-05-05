#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include "common.h"
#include "worker.h"
#include <signal.h> 
#include <errno.h>

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; 

worker_info_t workers[NUM_WORKERS];
volatile bool keep_running = true;

int main() {
    srand(time(NULL));
    
    printf("[MANAGER]: Initializing system with %d workers...\n", NUM_WORKERS);
    
    
    for (int i = 0; i < NUM_WORKERS; i++) {
        workers[i].id = i;
        workers[i].is_active = true;
        workers[i].last_heartbeat = time(NULL);
        
        if (pthread_create(&workers[i].thread, NULL, worker_routine, &workers[i]) != 0) {
            perror("[MANAGER]: Failed to create worker thread");
            return EXIT_FAILURE;
        }
    }

    printf("[MANAGER]: All workers started. Monitoring active...\n");

    int iterations = 0;
    while (iterations < 10) { 
        sleep(1); 
        
        for (int i = 0; i < NUM_WORKERS; i++) {
            
            int res = pthread_tryjoin_np(workers[i].thread, NULL);

            if (!workers[i].is_active && res != 0) {
                pthread_join(workers[i].thread, NULL); 
                res = 0; 
            }
            
            if (res == 0) { 
                pthread_mutex_lock(&log_mutex);
                printf("[MANAGER]: Detected failure in Worker %d. Restarting...\n", i);
                pthread_mutex_unlock(&log_mutex);
                
                workers[i].is_active = true;
                workers[i].last_heartbeat = time(NULL);
                pthread_create(&(workers[i].thread), NULL, worker_routine, &workers[i]);
            }
        }
        iterations++;
    }

    printf("[MANAGER]: Shutting down system...\n");
    keep_running = false;

    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(workers[i].thread, NULL); 
    }

    pthread_mutex_destroy(&log_mutex);
    printf("[MANAGER]: System halted successfully.\n");

    return EXIT_SUCCESS;
}
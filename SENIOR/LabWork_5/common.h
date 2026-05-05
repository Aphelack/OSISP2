#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdint.h>

#define MAX_THREADS 1024

#define MAX_ITERATIONS 10000000

#define DEFAULT_THREAD_DELAY_MS 100

typedef enum {
    LOAD_CPU,        
    LOAD_MEMORY,    
    LOAD_MIXED      
} load_type_t;

typedef struct {
    int thread_id;
    uint64_t iterations;
    double result;
    double elapsed_time; 
    int is_active;
} thread_stats_t;

typedef struct {
    int total_threads;
    int active_threads;
    uint64_t total_iterations;
    double total_time;
    load_type_t load_type;
    thread_stats_t threads[MAX_THREADS];
} global_stats_t;

typedef struct {
    int initial_threads;      /* Начальное количество потоков */
    int max_threads;          /* Максимальное количество потоков */
    int step_threads;         /* Шаг увеличения потоков */
    int delay_ms;             /* Задержка между шагами (мс) */
    int iterations_per_thread;/* Итераций на поток */
    load_type_t load_type;    /* Тип нагрузки */
    int verbose;              /* Подробный вывод */
    int monitor;              /* Включить мониторинг */
} stress_params_t;

typedef void* (*load_func_t)(void*);

load_func_t get_load_function(load_type_t type);
const char* load_type_to_string(load_type_t type);

#endif /* COMMON_H */

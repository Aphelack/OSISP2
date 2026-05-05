#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <sys/resource.h>
#include <pthread.h>

#include "common.h"

static volatile sig_atomic_t g_running = 1;
static volatile int g_pause = 0;

static global_stats_t g_stats;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
    } else if (sig == SIGUSR1) {
        g_pause = !g_pause;
    }
}

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void* cpu_load(void* arg) {
    thread_stats_t* stats = (thread_stats_t*)arg;
    volatile double result = 0.0;
    double start_time = get_time_sec();
    
    for (uint64_t i = 0; i < stats->iterations && g_running; i++) {
        if (g_pause) {
            usleep(10000);
            i--;
            continue;
        }
        
        result += sin(i * 0.001) * cos(i * 0.002);
        result += sqrt(fabs(result) + 1.0);
        
        if (result > 1e15 || result < -1e15) {
            result = 0.0;
        }
    }
    
    stats->result = result;
    stats->elapsed_time = get_time_sec() - start_time;
    stats->is_active = 0;
    
    return NULL;
}

static void* memory_load(void* arg) {
    thread_stats_t* stats = (thread_stats_t*)arg;
    double start_time = get_time_sec();
    const size_t chunk_size = 1024; /* 1 KB */
    volatile size_t sum = 0;
    
    for (uint64_t i = 0; i < stats->iterations && g_running; i++) {
        if (g_pause) {
            usleep(10000);
            i--;
            continue;
        }
        
        char* chunk = malloc(chunk_size);
        if (chunk) {
            for (size_t j = 0; j < chunk_size; j++) {
                chunk[j] = (char)(i & 0xFF);
            }
            for (size_t j = 0; j < chunk_size; j++) {
                sum += chunk[j];
            }
            free(chunk);
        }
    }
    
    stats->result = (double)sum;
    stats->elapsed_time = get_time_sec() - start_time;
    stats->is_active = 0;
    
    return NULL;
}

static void* mixed_load(void* arg) {
    thread_stats_t* stats = (thread_stats_t*)arg;
    volatile double result = 0.0;
    double start_time = get_time_sec();
    const size_t chunk_size = 512;
    
    for (uint64_t i = 0; i < stats->iterations && g_running; i++) {
        if (g_pause) {
            usleep(10000);
            i--;
            continue;
        }
        
        result += sin(i * 0.001) * cos(i * 0.002);
        
        char* chunk = malloc(chunk_size);
        if (chunk) {
            for (size_t j = 0; j < chunk_size; j++) {
                chunk[j] = (char)(i & 0xFF);
            }
            free(chunk);
        }
        
        if (result > 1e15 || result < -1e15) {
            result = 0.0;
        }
    }
    
    stats->result = result;
    stats->elapsed_time = get_time_sec() - start_time;
    stats->is_active = 0;
    
    return NULL;
}

load_func_t get_load_function(load_type_t type) {
    switch (type) {
        case LOAD_CPU:    return cpu_load;
        case LOAD_MEMORY: return memory_load;
        case LOAD_MIXED:  return mixed_load;
        default:          return cpu_load;
    }
}

const char* load_type_to_string(load_type_t type) {
    switch (type) {
        case LOAD_CPU:    return "CPU";
        case LOAD_MEMORY: return "MEMORY";
        case LOAD_MIXED:  return "MIXED";
        default:          return "UNKNOWN";
    }
}

static double get_process_cpu_usage(pid_t pid) {
    FILE* stat;
    char path[256];
    long utime, stime;
    static long prev_utime = 0, prev_stime = 0;
    static double prev_time = 0;
    
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    stat = fopen(path, "r");
    if (!stat) return 0.0;
    
    for (int i = 0; i < 13; i++) {
        if (fscanf(stat, "%*s ") == EOF) break;
    }
    if (fscanf(stat, "%ld %ld", &utime, &stime) != 2) {
        fclose(stat);
        return 0.0;
    }
    fclose(stat);
    
    double curr_time = get_time_sec();
    double delta_time = curr_time - prev_time;
    
    if (delta_time > 0 && prev_time > 0) {
        long delta_utime = utime - prev_utime;
        long delta_stime = stime - prev_stime;
        double cpu_usage = ((delta_utime + delta_stime) / (double)sysconf(_SC_CLK_TCK)) / delta_time * 100.0;
        
        prev_utime = utime;
        prev_stime = stime;
        prev_time = curr_time;
        
        return cpu_usage;
    }
    
    prev_utime = utime;
    prev_stime = stime;
    prev_time = curr_time;
    return 0.0;
}

static long get_process_memory_kb(pid_t pid) {
    FILE* statm;
    char path[256];
    long vm_size, vm_rss;
    
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    statm = fopen(path, "r");
    if (!statm) return 0;
    
    if (fscanf(statm, "%ld %ld", &vm_size, &vm_rss) != 2) {
        fclose(statm);
        return 0;
    }
    fclose(statm);
    
    long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
    return vm_rss * page_size_kb;
}

static void print_stats(void) {
    pthread_mutex_lock(&g_stats_mutex);

    printf("\nСтатистика\n");
    printf("Тип нагрузки:    %s\n", load_type_to_string(g_stats.load_type));
    printf("Всего потоков:   %d\n", g_stats.total_threads);
    printf("Активных:        %d\n", g_stats.active_threads);
    printf("Итераций всего:  %lu\n", (unsigned long)g_stats.total_iterations);
    printf("Время работы:    %.2f сек\n", g_stats.total_time);
    
    if (g_stats.total_threads > 0) {
        uint64_t completed = 0;
        for (int i = 0; i < g_stats.total_threads; i++) {
            if (!g_stats.threads[i].is_active) {
                completed++;
            }
        }
        printf("Завершено:       %lu / %d\n", (unsigned long)completed, g_stats.total_threads);
    }
        
    pthread_mutex_unlock(&g_stats_mutex);
}

typedef struct {
    int thread_id;
    uint64_t iterations;
    thread_stats_t* stats;
} thread_arg_t;

static int run_load_step(int num_threads, uint64_t iterations, load_func_t load_func) {
    pthread_t threads[MAX_THREADS];
    thread_stats_t stats[MAX_THREADS];
    
    if (num_threads > MAX_THREADS) {
        fprintf(stderr, "ERROR: Too many threads (max %d)\n", MAX_THREADS);
        return -1;
    }
    
    printf("\n[STEP] Запуск %d потоков, %lu итераций каждый...\n", 
           num_threads, (unsigned long)iterations);
    
    for (int i = 0; i < num_threads; i++) {
        stats[i].thread_id = i;
        stats[i].iterations = iterations;
        stats[i].result = 0;
        stats[i].elapsed_time = 0;
        stats[i].is_active = 1;
    }
    
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.active_threads = num_threads;
    pthread_mutex_unlock(&g_stats_mutex);
    
    double start_time = get_time_sec();
    
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, load_func, &stats[i]) != 0) {
            fprintf(stderr, "ERROR: Failed to create thread %d\n", i);
            g_running = 0;
            break;
        }
    }
    
    for (int i = 0; i < num_threads && g_running; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double elapsed = get_time_sec() - start_time;
    
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.active_threads = 0;
    g_stats.total_time += elapsed;
    for (int i = 0; i < num_threads; i++) {
        g_stats.total_iterations += stats[i].iterations;
    }
    pthread_mutex_unlock(&g_stats_mutex);
    
    printf("[STEP] Завершено за %.2f сек\n", elapsed);
    
    double total_cpu_time = 0;
    for (int i = 0; i < num_threads; i++) {
        total_cpu_time += stats[i].elapsed_time;
        if (g_stats.total_threads < MAX_THREADS) {
            g_stats.threads[g_stats.total_threads++] = stats[i];
        }
    }
    
    double avg_time = total_cpu_time / num_threads;
    double throughput = (double)(num_threads * iterations) / elapsed;
    
    printf("  Среднее время потока: %.3f сек\n", avg_time);
    printf("  Пропускная способность: %.0f итер/сек\n", throughput);
    
    return 0;
}

static int parse_args(int argc, char* argv[], stress_params_t* params) {
    params->initial_threads = 2;
    params->max_threads = 20;
    params->step_threads = 2;
    params->delay_ms = 500;
    params->iterations_per_thread = 100000;
    params->load_type = LOAD_CPU;
    params->verbose = 0;
    params->monitor = 1;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return -1;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (++i < argc) params->initial_threads = atoi(argv[i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max") == 0) {
            if (++i < argc) params->max_threads = atoi(argv[i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--step") == 0) {
            if (++i < argc) params->step_threads = atoi(argv[i]);
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delay") == 0) {
            if (++i < argc) params->delay_ms = atoi(argv[i]);
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--iter") == 0) {
            if (++i < argc) params->iterations_per_thread = atoi(argv[i]);
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--load") == 0) {
            if (++i < argc) {
                if (strcmp(argv[i], "cpu") == 0) params->load_type = LOAD_CPU;
                else if (strcmp(argv[i], "memory") == 0) params->load_type = LOAD_MEMORY;
                else if (strcmp(argv[i], "mixed") == 0) params->load_type = LOAD_MIXED;
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            params->verbose = 1;
        }
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    stress_params_t params;
    pid_t pid = getpid();

    if (parse_args(argc, argv, &params) < 0) {
        return EXIT_SUCCESS;
    }
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGUSR1, handle_signal);
    
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.load_type = params.load_type;
    
    printf("\nConfiguration:\n");
    printf("  PID:               %d\n", pid);
    printf("  Initial threads:   %d\n", params.initial_threads);
    printf("  Maximum threads:   %d\n", params.max_threads);
    printf("  Step increment:    %d\n", params.step_threads);
    printf("  Delay between steps: %d ms\n", params.delay_ms);
    printf("  Iterations/thread: %lu\n", (unsigned long)params.iterations_per_thread);
    printf("  Load type:         %s\n", load_type_to_string(params.load_type));
    printf("\n");
    
    load_func_t load_func = get_load_function(params.load_type);
    
    int current_threads = params.initial_threads;
    int step = 1;
    
    while (g_running && current_threads <= params.max_threads) {
        double cpu_usage = get_process_cpu_usage(pid);
        long mem_kb = get_process_memory_kb(pid);
        
        printf("\n[STEP %d] Threads: %d | CPU: %.1f%% | Memory: %ld KB\n", 
               step, current_threads, cpu_usage, mem_kb);
        
        if (run_load_step(current_threads, params.iterations_per_thread, load_func) < 0) {
            break;
        }
        
        int delay_remaining = params.delay_ms;
        while (g_running && delay_remaining > 0) {
            int sleep_time = (delay_remaining > 100) ? 100 : delay_remaining;
            usleep(sleep_time * 1000);
            delay_remaining -= sleep_time;
        }
        
        current_threads += params.step_threads;
        step++;
    }
    
    print_stats();
    
    printf("\nTest completed.\n");
    printf("Log file: stress_test.log (if created)\n");
    
    return EXIT_SUCCESS;
}

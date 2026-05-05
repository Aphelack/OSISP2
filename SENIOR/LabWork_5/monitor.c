#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/sysinfo.h>
#include <signal.h>

static volatile sig_atomic_t g_running = 1;

void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static void print_system_info(void) {
    struct sysinfo info;
    long pages_total, pages_free, page_size;
    
    if (sysinfo(&info) == 0) {
        page_size = sysconf(_SC_PAGESIZE);
        pages_total = info.totalram;
        pages_free = info.freeram;
        
        printf("\nSYSTEM INFOn");
        printf("Uptime:        %ld seconds (%.2f hours)\n", 
               info.uptime, info.uptime / 3600.0);
        printf("Total RAM:     %.2f GB\n", 
               (pages_total * page_size) / (1024.0 * 1024.0 * 1024.0));
        printf("Free RAM:      %.2f GB\n", 
               (pages_free * page_size) / (1024.0 * 1024.0 * 1024.0));
        printf("Used RAM:      %.2f GB (%.1f%%)\n", 
               ((pages_total - pages_free) * page_size) / (1024.0 * 1024.0 * 1024.0),
               ((pages_total - pages_free) * 100.0) / pages_total);
        printf("Total Swap:    %.2f GB\n", 
               (info.totalswap * page_size) / (1024.0 * 1024.0 * 1024.0));
        printf("Free Swap:     %.2f GB\n", 
               (info.freeswap * page_size) / (1024.0 * 1024.0 * 1024.0));
        printf("Processes:     %d\n", info.procs);
    }
}

static int get_cpu_count(void) {
    return sysconf(_SC_NPROCESSORS_ONLN);
}


static int read_cpu_stats(long* user, long* nice, long* system, long* idle) {
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return -1;
    
    if (fscanf(f, "cpu %ld %ld %ld %ld", user, nice, system, idle) != 4) {
        fclose(f);
        return -1;
    }
    
    fclose(f);
    return 0;
}

static int read_mem_stats(long* total, long* free, long* available) {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    
    char line[256];
    *total = *free = *available = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%ld", total);
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line + 8, "%ld", free);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%ld", available);
        }
    }
    
    fclose(f);
    return 0;
}

static int get_process_count(void) {
    DIR* proc = opendir("/proc");
    if (!proc) return -1;
    
    int count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_type == DT_DIR) {
            int is_pid = 1;
            for (char* p = entry->d_name; *p; p++) {
                if (*p < '0' || *p > '9') {
                    is_pid = 0;
                    break;
                }
            }
            if (is_pid) count++;
        }
    }
    
    closedir(proc);
    return count;
}

static double calculate_cpu_usage(long prev_user, long prev_nice, long prev_system, 
                                   long prev_idle,
                                   long curr_user, long curr_nice, long curr_system, 
                                   long curr_idle) {
    long prev_total = prev_user + prev_nice + prev_system + prev_idle;
    long curr_total = curr_user + curr_nice + curr_system + curr_idle;
    
    long total_diff = curr_total - prev_total;
    long idle_diff = curr_idle - prev_idle;
    
    if (total_diff == 0) return 0.0;
    
    return (100.0 * (total_diff - idle_diff)) / total_diff;
}

static void run_monitor(int interval_sec, int duration_sec) {
    long prev_user, prev_nice, prev_system, prev_idle;
    long curr_user, curr_nice, curr_system, curr_idle;
    long mem_total, mem_free, mem_available;
    int iterations = duration_sec / interval_sec;
    
    printf("\nStarting system monitor (interval: %ds, duration: %ds)...\n", 
           interval_sec, duration_sec);
    
    /* Инициализация */
    if (read_cpu_stats(&prev_user, &prev_nice, &prev_system, &prev_idle) < 0) {
        fprintf(stderr, "ERROR: Cannot read CPU stats\n");
        return;
    }
    
    printf("Time     | CPU%%  | RAM Total | RAM Free  | RAM Avail | Processes\n");
    printf("---------|--------|-----------|-----------|-----------|----------\n");
    
    for (int i = 0; i < iterations && g_running; i++) {
        sleep(interval_sec);
        
        char timestamp[32];
        get_timestamp(timestamp, sizeof(timestamp));
        
        if (read_cpu_stats(&curr_user, &curr_nice, &curr_system, &curr_idle) == 0) {
            double cpu_usage = calculate_cpu_usage(
                prev_user, prev_nice, prev_system, prev_idle,
                curr_user, curr_nice, curr_system, curr_idle
            );
            
            /* Чтение памяти */
            read_mem_stats(&mem_total, &mem_free, &mem_available);
            
            int proc_count = get_process_count();
            
            printf("%s | %6.1f | %9ld | %9ld | %9ld | %9d\n",
                   timestamp, cpu_usage, mem_total, mem_free, mem_available, proc_count);
            
            prev_user = curr_user;
            prev_nice = curr_nice;
            prev_system = curr_system;
            prev_idle = curr_idle;
        }
    }
}

static void show_help(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -i, --interval SEC  Monitoring interval (default: 2)\n");
    printf("  -d, --duration SEC  Monitoring duration (default: 60)\n");
    printf("  -s, --snapshot      Show system snapshot and exit\n");
    printf("  -h, --help          Show this help\n");
    printf("\nExamples:\n");
    printf("  %s                  # Monitor for 60 seconds\n", prog);
    printf("  %s -i 1 -d 120      # Monitor for 120 seconds with 1s interval\n", prog);
    printf("  %s --snapshot       # Show system info and exit\n", prog);
}

int main(int argc, char* argv[]) {
    int interval_sec = 2;
    int duration_sec = 60;
    int snapshot_only = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interval") == 0) {
            if (++i < argc) interval_sec = atoi(argv[i]);
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--duration") == 0) {
            if (++i < argc) duration_sec = atoi(argv[i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--snapshot") == 0) {
            snapshot_only = 1;
        }
    }
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("System Resource Monitor\n");
    printf("CPU Cores: %d\n", get_cpu_count());
    
    print_system_info();
    
    if (snapshot_only) {
        return EXIT_SUCCESS;
    }
    
    run_monitor(interval_sec, duration_sec);
    
    printf("\nMonitoring completed.\n");
    
    return EXIT_SUCCESS;
}

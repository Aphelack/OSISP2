#define _XOPEN_SOURCE 700 

#include "recovery.h"

static int counter = 0;

void handle_recovery(int sig) {
    pid_t pid = fork(); 

    if (pid < 0) {
        perror("Ошибка при вызове fork");
        exit(EXIT_FAILURE); 
    }

    if (pid > 0) {
        printf("\n[PID: %d] Получен сигнал %d. Клонирование...\n", getpid(), sig);
        exit(EXIT_SUCCESS); 
    } else {
        printf("[PID: %d] Процесс восстановлен. Продолжение...\n", getpid());
    }
}

void log_status(int pid, int counter) {
    FILE *f = fopen(LOG_FILE, "a");
    if (f == NULL) return;
    fprintf(f, "Time: %ld | PID: %d | Counter: %d\n", (long)time(NULL), pid, counter);
    fclose(f);
}

void start_main_loop(void) {
    while (1) {
        counter++;
        printf("PID: %d | Текущее значение: %d\n", getpid(), counter);
        log_status(getpid(), counter);
        sleep(2);
    }
}

int main() {
    struct sigaction sa;
    sa.sa_handler = handle_recovery;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; 

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    printf("Программа запущенна. PID: %d\n", getpid());
    start_main_loop();

    return 0;
}
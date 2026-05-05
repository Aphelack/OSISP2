#ifndef PROC_H
#define PROC_H

#include <stdio.h>
#include <sys/types.h>

typedef struct {
    pid_t pid;
    pid_t ppid;
    char name[256];
    char state;
    char state_str[32];
    unsigned long vsize;
    long rss;
    unsigned long utime;
    unsigned long stime;
    int threads;
    uid_t uid;
} proc_info_t;

/* Заполняет структуру proc_info_t по PID. Возвращает 0 при успехе, -1 при ошибке. */
int get_proc_info(pid_t pid, proc_info_t *info);

/* Выводит список всех процессов (PID и имя). Возвращает кол-во процессов. */
int list_all_procs(FILE *out);

/* Выводит подробную информацию о процессе. */
void print_proc_info(const proc_info_t *info, FILE *out);

#endif

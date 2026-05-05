#ifndef RECOVERY_H
#define RECOVERY_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

#define LOG_FILE "process_state.txt"

void handle_recovery(int sig);
void log_status(int pid, int counter);
void start_main_loop(void);

#endif
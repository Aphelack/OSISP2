#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "proc.h"

static void usage(const char *progname)
{
    fprintf(stderr, "Использование:\n");
    fprintf(stderr, "  %s -l [-o файл]        — список всех процессов\n", progname);
    fprintf(stderr, "  %s -p <pid> [-o файл]  — информация о процессе\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    int opt;
    int mode = 0;
    pid_t target_pid = 0;
    const char *outpath = NULL;

    while ((opt = getopt(argc, argv, "lp:o:")) != -1) {
        switch (opt) {
        case 'l':
            mode = 1;
            break;
        case 'p':
            mode = 2;
            target_pid = atoi(optarg);
            if (target_pid <= 0) {
                fprintf(stderr, "Ошибка: некорректный PID '%s'\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'o':
            outpath = optarg;
            break;
        default:
            usage(argv[0]);
        }
    }

    if (mode == 0)
        usage(argv[0]);

    FILE *out = stdout;
    if (outpath) {
        out = fopen(outpath, "w");
        if (!out) {
            perror("fopen");
            return EXIT_FAILURE;
        }
    }

    if (mode == 1) {
        list_all_procs(out);
    } else {
        proc_info_t info;
        if (get_proc_info(target_pid, &info) != 0) {
            fprintf(stderr, "Ошибка: не удалось получить информацию о процессе %d.\n"
                            "Процесс не существует или нет прав доступа.\n", target_pid);
            if (outpath) fclose(out);
            return EXIT_FAILURE;
        }
        print_proc_info(&info, out);
    }

    if (outpath) {
        fclose(out);
        printf("Результат записан в %s\n", outpath);
    }

    return EXIT_SUCCESS;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "censor.h"
#define _GNU_SOURCE
#include <getopt.h>  

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s -d <dict_file> [-o <out_file>] [-r <censor_word>]\n", progname);
    fprintf(stderr, "Reads stdin, censors words from dict, writes to stdout or out_file\n");
}

int main(int argc, char *argv[]) {
    char *dict_file = NULL;
    char *out_file = NULL;
    char *replacement = "[CENSORED]";
    int opt;

    while ((opt = getopt(argc, argv, "d:o:r:")) != -1) {
        switch (opt) {
            case 'd':
                dict_file = optarg;
                break;
            case 'o':
                out_file = optarg;
                break;
            case 'r':
                replacement = optarg;
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!dict_file) {
        print_usage(argv[0]);
        return 1;
    }

    if (load_dict(dict_file) <= 0) {
        free_dict();
        fprintf(stderr, "Failed to load dictionary from %s\n", dict_file);
        return 1;
    }

    FILE *out = stdout;
    if (out_file) {
        out = fopen(out_file, "w");
        if (!out) {
            perror("fopen out_file");
            free_dict();
            return 1;
        }
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, stdin)) != -1) {
        line[strcspn(line, "\n")] = '\0';
        char *censored = censor_text(line, replacement);
        if (censored) {
            fprintf(out, "%s\n", censored);
            free(censored);
        } else {
            fprintf(out, "%s\n", line);
        }
    }

    free(line);
    if (out != stdout) fclose(out);
    free_dict();
    return 0;
}
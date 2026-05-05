#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "censor.h"
#include <ctype.h> 
#define MAX_WORD_LEN 256
#define MAX_DICT_WORDS 1000

static char *dictionary[MAX_DICT_WORDS];
static int dict_size = 0;

int load_dict(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[MAX_WORD_LEN];
    while (fgets(line, sizeof(line), f) && dict_size < MAX_DICT_WORDS) {
        line[strcspn(line, "\n")] = '\0';
        dictionary[dict_size] = strdup(line);
        if (!dictionary[dict_size]) {
            fclose(f);
            return -1;
        }
        dict_size++;
    }
    fclose(f);
    return dict_size;
}

void free_dict(void) {
    for (int i = 0; i < dict_size; i++) {
        free(dictionary[i]);
    }
    dict_size = 0;
}

char *censor_text(const char *text, const char *replacement) {
    if (!text || dict_size == 0) return strdup(text);

    char *result = strdup(text);
    if (!result) return NULL;

    for (int i = 0; i < dict_size; i++) {
        char *pos = result;
        int word_len = strlen(dictionary[i]);
        int repl_len = strlen(replacement);

        while ((pos = strstr(pos, dictionary[i])) != NULL) {
            
            int left_ok = (pos == result) || !isalnum((unsigned char)pos[-1]);
     
            int right_ok = (pos[word_len] == '\0') || !isalnum((unsigned char)pos[word_len]);

            if (!left_ok || !right_ok) {
                pos += 1; 
                continue;
            }

            char *new_result = malloc(strlen(result) - word_len + repl_len + 1);
            if (!new_result) {
                free(result);
                return NULL;
            }
            strncpy(new_result, result, pos - result);
            new_result[pos - result] = '\0';
            strcat(new_result, replacement);
            strcat(new_result, pos + word_len);
            
            size_t offset = (pos - result) + repl_len;
            free(result);
            result = new_result;
            pos = result + offset;
        }
    }
    return result;
}
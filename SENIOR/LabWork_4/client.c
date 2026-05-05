#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>

#include "common.h"

static volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static void client_log(const char *format, ...) {
    va_list args;
    char timestamp[32];
    
    get_timestamp(timestamp, sizeof(timestamp));
    
    fprintf(stderr, "[%s] [Client %d] ", timestamp, getpid());
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static void show_help(void) {
    printf("\nAvailable commands:\n");
    printf("  echo <text>     - Echo the text back\n");
    printf("  upper <text>    - Convert text to UPPERCASE\n");
    printf("  lower <text>    - Convert text to lowercase\n");
    printf("  length <text>   - Get length of text\n");
    printf("  reverse <text>  - Reverse the text\n");
    printf("  quit            - Exit client\n");
    printf("  help            - Show this help\n");
    printf("\n");
}

static int send_request(request_type_t type, const char *data, char *response) {
    request_t req;
    response_t resp;
    char client_fifo[FIFO_PATH_MAX];
    int server_fd, client_fd;
    ssize_t bytes;
    
    req.client_pid = getpid();
    req.request_type = type;
    strncpy(req.data, data, sizeof(req.data) - 1);
    req.data[sizeof(req.data) - 1] = '\0';
    
    snprintf(client_fifo, sizeof(client_fifo), "/tmp/lab4_client_%d_fifo", getpid());
    unlink(client_fifo);
    
    if (mkfifo(client_fifo, 0666) < 0) {
        client_log("ERROR: Cannot create client FIFO: %s", strerror(errno));
        return -1;
    }
    
    client_log("Sending request: type=%d, data=\"%s\"", type, data);
    
    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd < 0) {
        client_log("ERROR: Cannot open server FIFO: %s", strerror(errno));
        unlink(client_fifo);
        return -1;
    }
    
    bytes = write(server_fd, &req, sizeof(req));
    close(server_fd);
    
    if (bytes != sizeof(req)) {
        client_log("ERROR: Failed to send request");
        unlink(client_fifo);
        return -1;
    }
    
    client_log("Request sent, waiting for response...");
    
    client_fd = open(client_fifo, O_RDONLY);
    if (client_fd < 0) {
        client_log("ERROR: Cannot open client FIFO for reading: %s", strerror(errno));
        unlink(client_fifo);
        return -1;
    }
    
    bytes = read(client_fd, &resp, sizeof(resp));
    close(client_fd);
    unlink(client_fifo); /* Удаляем FIFO после использования */
    
    if (bytes != sizeof(resp)) {
        client_log("ERROR: Failed to receive response");
        return -1;
    }
    
    if (resp.status != 0) {
        client_log("Server returned error: %s", resp.response);
        return -1;
    }
    
    strncpy(response, resp.response, MSG_SIZE - 1);
    response[MSG_SIZE - 1] = '\0';
    
    client_log("Response received: \"%s\"", response);
    return 0;
}

static int parse_command(const char *input, request_type_t *type, char *data) {
    char cmd[32];
    const char *text_start;
    
    if (sscanf(input, "%31s", cmd) != 1) {
        return -1;
    }
    
    text_start = input + strlen(cmd);
    while (*text_start == ' ') text_start++;
    
    /* Определяем тип запроса */
    if (strcmp(cmd, "echo") == 0) {
        *type = REQ_ECHO;
    } else if (strcmp(cmd, "upper") == 0) {
        *type = REQ_UPPER;
    } else if (strcmp(cmd, "lower") == 0) {
        *type = REQ_LOWER;
    } else if (strcmp(cmd, "length") == 0) {
        *type = REQ_LENGTH;
    } else if (strcmp(cmd, "reverse") == 0) {
        *type = REQ_REVERSE;
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        *type = REQ_QUIT;
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        show_help();
        return -1;
    } else {
        printf("Unknown command: '%s'. Type 'help' for available commands.\n", cmd);
        return -1;
    }
    
    if (*type == REQ_QUIT) {
        data[0] = '\0';
    } else {
        strncpy(data, text_start, MSG_SIZE - 1);
        data[MSG_SIZE - 1] = '\0';
        
        if (strlen(data) == 0 && *type != REQ_QUIT) {
            strcpy(data, "Hello, Server!");
        }
    }
    
    return 0;
}

static int interactive_mode(void) {
    char input[512];
    request_type_t type;
    char data[MSG_SIZE];
    char response[MSG_SIZE];
    
    printf("\n=== Client %d connected ===\n", getpid());
    show_help();
    
    while (running) {
        printf("\n> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        input[strcspn(input, "\n")] = '\0';
        
        if (strlen(input) == 0) {
            continue;
        }
        
        if (parse_command(input, &type, data) < 0) {
            if (type == REQ_QUIT) {
                break;
            }
            continue;
        }
        
        if (send_request(type, data, response) < 0) {
            printf("ERROR: Failed to communicate with server\n");
            continue;
        }
        
        printf("Response: %s\n", response);
    }
    
    return 0;
}


static int single_command_mode(int argc, char *argv[]) {
    request_type_t type;
    char data[MSG_SIZE];
    char response[MSG_SIZE];
    char input[512];
    
    input[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(input, " ");
        strcat(input, argv[i]);
    }
    
    if (parse_command(input, &type, data) < 0) {
        return EXIT_FAILURE;
    }
    
    if (send_request(type, data, response) < 0) {
        return EXIT_FAILURE;
    }
    
    printf("%s\n", response);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    struct stat st;
    if (stat(SERVER_FIFO, &st) < 0) {
        fprintf(stderr, "ERROR: Server FIFO not found: %s\n", SERVER_FIFO);
        fprintf(stderr, "Make sure the server is running.\n");
        return EXIT_FAILURE;
    }
    
    if (argc > 1) {
        return single_command_mode(argc, argv);
    } else {
        return interactive_mode();
    }
}

#include "io.h"

#include <errno.h>
#include <unistd.h>

ssize_t read_full(int fd, void *buf, size_t count) {
    unsigned char *p = buf;
    size_t left = count;

    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }
    return (ssize_t)(count - left);
}

ssize_t write_full(int fd, const void *buf, size_t count) {
    const unsigned char *p = buf;
    size_t left = count;

    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }
    return (ssize_t)count;
}

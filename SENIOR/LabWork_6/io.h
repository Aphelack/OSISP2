#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <sys/types.h>

ssize_t read_full(int fd, void *buf, size_t count);
ssize_t write_full(int fd, const void *buf, size_t count);

#endif

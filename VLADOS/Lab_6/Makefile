CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
TARGET = scanner
SOURCES = main.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
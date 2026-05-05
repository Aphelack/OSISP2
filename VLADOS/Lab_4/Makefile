CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = recovery
OBJS = recovery.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

recovery.o: recovery.c recovery.h

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
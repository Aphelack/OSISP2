# 
CC = gcc
CFLAGS = -Wall -Wextra -pthread -std=c11
TARGET = manager_app

OBJ = main.o worker.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

main.o: main.c common.h worker.h
	$(CC) $(CFLAGS) -c main.c

worker.o: worker.c common.h worker.h
	$(CC) $(CFLAGS) -c worker.c

clean:
	rm -f *.o $(TARGET)

run: all
	./$(TARGET)
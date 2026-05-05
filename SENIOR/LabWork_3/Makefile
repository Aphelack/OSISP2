CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
TARGET  = myps
OBJS    = main.o proc.o
OUTFILE = test_output.txt

.PHONY: all clean test proc.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

main.o: main.c proc.h
	$(CC) $(CFLAGS) -c main.c

proc.o: proc.c proc.h
	$(CC) $(CFLAGS) -c proc.c

clean:
	rm -f $(OBJS) $(TARGET) $(OUTFILE)

test: $(TARGET)
	@echo "Тест: список процессов"
	./$(TARGET) -l | head -15
	@echo ""
	@echo "Тест: информация о PID 1"
	./$(TARGET) -p 1
	@echo ""
	@echo "Тест: вывод в файл"
	./$(TARGET) -l -o $(OUTFILE)
	head -10 $(OUTFILE)
	@echo ""
	@echo "Тест: несуществующий PID"
	./$(TARGET) -p 9999999 || true
	@echo ""
	@echo "Все тесты завершены."

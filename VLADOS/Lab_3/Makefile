CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = autocensor
OBJS = autocensor.o censor.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

autocensor.o: autocensor.c censor.h
censor.o: censor.c censor.h

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	@echo "TEST"	
	@echo "bad word here" > test_input.txt
	@echo "hello bad word world" >> test_input.txt
	@echo "good text" >> test_input.txt
	@./$(TARGET) -d dict.txt -o test_output.txt < test_input.txt
	@cat test_output.txt

.PHONY: all clean test
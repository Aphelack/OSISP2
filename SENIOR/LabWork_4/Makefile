CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
TARGETS = server client
SERVER  = server
CLIENT  = client
LOG_FILE = server.log

.PHONY: all clean test test-auto run-server stop

all: $(TARGETS)

server: server.c common.h
	$(CC) $(CFLAGS) -o $@ server.c

client: client.c common.h
	$(CC) $(CFLAGS) -o $@ client.c

clean:
	rm -f $(TARGETS) $(LOG_FILE)
	rm -f /tmp/lab4_*.fifo
	@echo "Cleaned up."

run-server: server
	@echo "Starting server..."
	./$(SERVER) &
	@sleep 1
	@echo "Server started with PID $$!"
	@echo "Use 'make stop' to stop the server."

stop:
	@pkill -f "./$(SERVER)" 2>/dev/null || true
	@rm -f /tmp/lab4_server_fifo
	@echo "Server stopped."

test: $(TARGETS)
	@echo "LabWork 4: IPC Server-Client Test"
	@echo ""
	@echo "[1/6] Cleanup..."
	@rm -f $(LOG_FILE) /tmp/lab4_*.fifo
	@echo "      Done."
	@echo ""
	@echo "[2/6] Starting server in background..."
	@./$(SERVER) &
	@sleep 1
	@SERVER_PID=$$(pgrep -f "./$(SERVER)" | head -1); \
	if [ -z "$$SERVER_PID" ]; then \
		echo "      ERROR: Server failed to start!"; \
		exit 1; \
	fi; \
	echo "      Server started with PID $$SERVER_PID"
	@echo ""
	@echo "[3/6] Testing single commands..."
	@echo "      Test ECHO:"
	@./$(CLIENT) echo "Hello World"
	@echo "      Test UPPER:"
	@./$(CLIENT) upper "hello world"
	@echo "      Test LOWER:"
	@./$(CLIENT) lower "HELLO WORLD"
	@echo "      Test LENGTH:"
	@./$(CLIENT) length "Hello"
	@echo "      Test REVERSE:"
	@./$(CLIENT) reverse "Hello"
	@echo ""
	@echo "[4/6] Testing multiple concurrent clients..."
	@( \
		./$(CLIENT) echo "Client 1 request" & \
		./$(CLIENT) upper "Client 2 request" & \
		./$(CLIENT) reverse "Client 3 request" & \
		./$(CLIENT) length "Client 4 request" & \
		wait \
	)
	@echo "      All concurrent requests completed."
	@echo ""
	@echo "[5/6] Server log:"
	@echo "----------------------------------------"
	@cat $(LOG_FILE) 2>/dev/null || echo "No log file found"
	@echo "----------------------------------------"
	@echo ""
	@echo "[6/6] Stopping server..."
	@pkill -TERM -f "./$(SERVER)" 2>/dev/null || true
	@sleep 0.5
	@rm -f /tmp/lab4_server_fifo
	@echo "      Server stopped."
	@echo ""

# Интерактивный тест с демонстрацией
demo: $(TARGETS)
	@echo "LabWork 4: Interactive Demo"
	@echo ""
	@echo "Step 1: Start the server in one terminal:"
	@echo "        make run-server"
	@echo ""
	@echo "Step 2: Run clients in another terminal:"
	@echo "        ./client                    # interactive mode"
	@echo "        ./client echo Hello         # single command"
	@echo ""
	@echo "Step 3: Stop the server when done:"
	@echo "        make stop"
	@echo ""

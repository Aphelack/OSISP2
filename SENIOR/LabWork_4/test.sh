#!/bin/bash

# LabWork 4: IPC Server-Client Test Script
# Тестирование системы с FIFO каналами

set -e

SERVER="./server"
CLIENT="./client"
LOG_FILE="server.log"
SERVER_FIFO="/tmp/lab4_server_fifo"

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

cleanup() {
    log_info "Cleaning up..."
    pkill -f "./server" 2>/dev/null || true
    rm -f "$SERVER_FIFO" /tmp/lab4_client_*_fifo 2>/dev/null || true
    rm -f "$LOG_FILE" 2>/dev/null || true
    sleep 0.5
}

wait_for_server() {
    local max_attempts=10
    local attempt=0
    
    while [ $attempt -lt $max_attempts ]; do
        if [ -p "$SERVER_FIFO" ]; then
            return 0
        fi
        sleep 0.5
        attempt=$((attempt + 1))
    done
    
    return 1
}

echo "========================================"
echo -e "${GREEN}=== LabWork 4: IPC Server-Client ===${NC}"
echo "========================================"
echo ""

# Проверка наличия исполняемых файлов
if [ ! -x "$SERVER" ] || [ ! -x "$CLIENT" ]; then
    log_error "Binaries not found. Run 'make' first."
    exit 1
fi

# Очистка перед тестом
cleanup

# Тест 1: Запуск сервера
echo "----------------------------------------"
log_info "Test 1: Starting server..."
$SERVER &
SERVER_PID=$!

if wait_for_server; then
    log_success "Server started with PID $SERVER_PID"
else
    log_error "Server failed to create FIFO"
    exit 1
fi
echo ""

# Тест 2: Одиночные запросы
echo "----------------------------------------"
log_info "Test 2: Single requests..."

echo -n "  ECHO test:       "
RESP=$($CLIENT echo "Hello World")
if [[ "$RESP" == "Hello World" ]]; then
    log_success "$RESP"
else
    log_error "Expected 'Hello World', got '$RESP'"
fi

echo -n "  UPPER test:      "
RESP=$($CLIENT upper "hello world")
if [[ "$RESP" == "HELLO WORLD" ]]; then
    log_success "$RESP"
else
    log_error "Expected 'HELLO WORLD', got '$RESP'"
fi

echo -n "  LOWER test:      "
RESP=$($CLIENT lower "HELLO WORLD")
if [[ "$RESP" == "hello world" ]]; then
    log_success "$RESP"
else
    log_error "Expected 'hello world', got '$RESP'"
fi

echo -n "  LENGTH test:     "
RESP=$($CLIENT length "Hello")
if [[ "$RESP" == "Length: 5" ]]; then
    log_success "$RESP"
else
    log_error "Expected 'Length: 5', got '$RESP'"
fi

echo -n "  REVERSE test:    "
RESP=$($CLIENT reverse "Hello")
if [[ "$RESP" == "olleH" ]]; then
    log_success "$RESP"
else
    log_error "Expected 'olleH', got '$RESP'"
fi

echo ""

# Тест 3: Множественные клиенты
echo "----------------------------------------"
log_info "Test 3: Concurrent clients (4 parallel requests)..."

PIDS=""
for i in 1 2 3 4; do
    $CLIENT echo "Request from client $i" &
    PIDS="$PIDS $!"
done

# Ждём завершения всех клиентов
FAILED=0
for pid in $PIDS; do
    if ! wait $pid; then
        FAILED=1
    fi
done

if [ $FAILED -eq 0 ]; then
    log_success "All concurrent requests completed"
else
    log_error "Some concurrent requests failed"
fi

echo ""

# Тест 4: Проверка логирования
echo "----------------------------------------"
log_info "Test 4: Checking server log..."

if [ -f "$LOG_FILE" ]; then
    LOG_LINES=$(wc -l < "$LOG_FILE")
    log_success "Log file contains $LOG_LINES entries"
    echo ""
    echo "  Last 10 log entries:"
    echo "  ----------------------------------------"
    tail -10 "$LOG_FILE" | sed 's/^/  /'
    echo "  ----------------------------------------"
else
    log_warn "Log file not found"
fi

echo ""

# Тест 5: Остановка сервера
echo "----------------------------------------"
log_info "Test 5: Stopping server..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

if [ ! -p "$SERVER_FIFO" ]; then
    log_success "Server stopped cleanly"
else
    log_warn "Server FIFO still exists"
    rm -f "$SERVER_FIFO"
fi

echo ""

# Финальный отчёт
echo "========================================"
log_success "All tests completed!"
echo "========================================"
echo ""
echo "To run interactive demo:"
echo "  1. Start server:  ./server &"
echo "  2. Run client:    ./client"
echo "  3. Try commands:  echo/upper/lower/length/reverse"
echo "  4. Exit client:   quit"
echo ""

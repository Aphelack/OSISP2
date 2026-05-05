#!/bin/bash

# LabWork 5: Multi-threaded Stress Test Script
# Тестирование многопоточной нагрузки на систему

set -e

STRESS="./stress_test"
MONITOR="./monitor"

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

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

log_header() {
    echo -e "\n${CYAN}========================================${NC}"
    echo -e "${CYAN}=== $1 ===${NC}"
    echo -e "${CYAN}========================================${NC}\n"
}

# Проверка наличия исполняемых файлов
check_binaries() {
    if [ ! -x "$STRESS" ] || [ ! -x "$MONITOR" ]; then
        log_error "Binaries not found. Run 'make' first."
        exit 1
    fi
}

# Тест 1: Система до нагрузки
test_system_snapshot() {
    log_header "Test 1: System Snapshot (Before Load)"
    $MONITOR --snapshot
}

# Тест 2: CPU нагрузка
test_cpu_load() {
    log_header "Test 2: CPU Load Test"
    log_info "Running CPU-intensive stress test..."
    $STRESS -t 2 -m 6 -s 2 -i 10000 -d 300 -l cpu
}

# Тест 3: Memory нагрузка
test_memory_load() {
    log_header "Test 3: Memory Load Test"
    log_info "Running memory-intensive stress test..."
    $STRESS -t 2 -m 6 -s 2 -i 500 -d 300 -l memory
}

# Тест 4: Mixed нагрузка
test_mixed_load() {
    log_header "Test 4: Mixed Load Test"
    log_info "Running mixed stress test..."
    $STRESS -t 2 -m 6 -s 2 -i 5000 -d 300 -l mixed
}

# Тест 5: Быстрое нарастание нагрузки
test_rapid_increase() {
    log_header "Test 5: Rapid Thread Increase Test"
    log_info "Testing rapid thread increase (2 -> 16 threads)..."
    $STRESS -t 2 -m 16 -s 4 -i 5000 -d 100 -l cpu
}

# Тест 6: Система после нагрузки
test_system_after() {
    log_header "Test 6: System Snapshot (After Load)"
    $MONITOR --snapshot
}

# Основной тест
run_full_test() {
    log_header "LabWork 5: Full Stress Test Suite"
    
    check_binaries
    
    echo "System Information:"
    echo "  CPU Cores: $(nproc 2>/dev/null || echo 'unknown')"
    echo "  Hostname:  $(hostname 2>/dev/null || echo 'unknown')"
    echo ""
    
    # Тесты
    test_system_snapshot
    test_cpu_load
    test_memory_load
    test_mixed_load
    test_rapid_increase
    test_system_after
    
    log_header "All Tests Completed"
    log_success "Stress test suite finished successfully!"
    
    echo ""
    echo "To run more tests:"
    echo "  make test-quick     - Quick test (2->4 threads)"
    echo "  make test-full      - Full test (2->20 threads)"
    echo "  make test-all       - Test all load types"
    echo ""
    echo "To monitor system in real-time:"
    echo "  ./monitor -i 1 -d 60"
    echo ""
}

# Показать помощь
show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -f, --full      Run full test suite"
    echo "  -c, --cpu       Run CPU load test only"
    echo "  -m, --memory    Run memory load test only"
    echo "  -x, --mixed     Run mixed load test only"
    echo "  -s, --snapshot  Show system snapshot"
    echo "  -h, --help      Show this help"
    echo ""
}

# Парсинг аргументов
case "${1:-}" in
    -f|--full)
        run_full_test
        ;;
    -c|--cpu)
        check_binaries
        test_cpu_load
        ;;
    -m|--memory)
        check_binaries
        test_memory_load
        ;;
    -x|--mixed)
        check_binaries
        test_mixed_load
        ;;
    -s|--snapshot)
        check_binaries
        test_system_snapshot
        ;;
    -h|--help)
        show_help
        ;;
    *)
        run_full_test
        ;;
esac

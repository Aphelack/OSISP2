CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -O2
LDFLAGS = -pthread -lm

TARGETS = stress_test monitor
STRESS  = stress_test
MONITOR = monitor

.PHONY: all clean test test-quick test-full demo

all: $(TARGETS)

stress_test: stress_test.c common.h
	$(CC) $(CFLAGS) -o $@ stress_test.c $(LDFLAGS)

monitor: monitor.c
	$(CC) $(CFLAGS) -o $@ monitor.c

clean:
	rm -f $(TARGETS) *.log
	@echo "Cleaned up."

test-quick: $(TARGETS)
	@echo "Quick Test (2->4 threads)"
	@echo ""
	@./$(STRESS) -t 2 -m 4 -s 2 -i 5000 -d 200 -l cpu
	@echo "Test completed"

test-full: $(TARGETS)
	@echo "Full Stress Test (2->20 threads)"
	@echo ""
	@echo "Starting stress test"
	@echo "Monitor system resources in another terminal:"
	@echo "  ./monitor -i 1 -d 120"
	@echo ""
	@./$(STRESS) -t 2 -m 20 -s 2 -i 50000 -d 500 -l cpu
	@echo ""
	@echo "Full test completed"

test-all: $(TARGETS)
	@echo "Testing all load types"
	@echo ""
	@echo "CPU Load Test"
	@./$(STRESS) -t 2 -m 6 -s 2 -i 10000 -d 300 -l cpu
	@echo ""
	@echo "Memory Load Test"
	@./$(STRESS) -t 2 -m 6 -s 2 -i 1000 -d 300 -l memory
	@echo ""
	@echo "Mixed Load Test"
	@./$(STRESS) -t 2 -m 6 -s 2 -i 5000 -d 300 -l mixed
	@echo ""
	@echo "All load types tested"

demo: $(TARGETS)
	@echo "Interactive Demo"
	@echo ""
	@echo "Step 1: Start system monitor (in background):"
	@echo "        ./monitor -i 1 -d 120 &"
	@echo ""
	@echo "Step 2: Run stress test:"
	@echo "        ./stress_test -t 2 -m 10 -s 2 -i 50000 -d 1000"
	@echo ""
	@echo "Step 3: Observe system load increasing"
	@echo ""
	@echo "Step 4: To pause/resume stress test:"
	@echo "        kill -USR1 <stress_test_pid>"
	@echo ""

test-with-monitor: $(TARGETS)
	@echo "Stress Test with Monitoring"
	@echo ""
	@echo "Starting monitor in background..."
	@./$(MONITOR) -i 1 -d 30 -s > system_snapshot.txt &
	@sleep 1
	@echo "Starting stress test..."
	@./$(STRESS) -t 2 -m 8 -s 2 -i 20000 -d 500 -l cpu
	@echo ""
	@echo "System snapshot saved to: system_snapshot.txt"
	@cat system_snapshot.txt

# LabWork 5. Стресс-тестирование и мониторинг ресурсов

## Идея лабораторной

Лабораторная состоит из двух программ:

1. `stress_test` создает управляемую нагрузку на CPU, память или сразу на оба ресурса.
2. `monitor` читает системные показатели из Linux и показывает загрузку CPU, память и количество процессов.

Главная идея — увидеть связь между действиями программы и состоянием операционной системы. Стресс-тест увеличивает число потоков и выполняет нагрузочные операции, а монитор позволяет наблюдать, как меняются ресурсы.

## Как собрать и запускать

Из каталога `LabWork_5`:

```bash
make
```

Быстрый CPU-тест:

```bash
./stress_test -t 2 -m 4 -s 2 -i 5000 -d 200 -l cpu
```

Тест памяти:

```bash
./stress_test -t 2 -m 6 -s 2 -i 1000 -d 300 -l memory
```

Смешанная нагрузка:

```bash
./stress_test -t 2 -m 6 -s 2 -i 5000 -d 300 -l mixed
```

Мониторинг:

```bash
./monitor -i 1 -d 60
```

Снимок системы без длительного мониторинга:

```bash
./monitor --snapshot
```

Готовые цели из `Makefile`:

```bash
make test-quick
make test-full
make test-all
make test-with-monitor
```

## Параметры стресс-теста

`stress_test` принимает такие основные опции:

```text
-t, --threads   начальное количество потоков
-m, --max       максимальное количество потоков
-s, --step      шаг увеличения потоков
-d, --delay     задержка между шагами в миллисекундах
-i, --iter      количество итераций на поток
-l, --load      тип нагрузки: cpu, memory, mixed
```

Например:

```bash
./stress_test -t 2 -m 10 -s 2 -i 50000 -d 500 -l cpu
```

Это означает: начать с 2 потоков, дойти до 10, увеличивать по 2 потока, каждому потоку дать 50000 итераций, между шагами ждать 500 мс.

## Привязка к коду: типы нагрузки

Типы нагрузки описаны в `common.h`:

```c
typedef enum {
    LOAD_CPU,
    LOAD_MEMORY,
    LOAD_MIXED
} load_type_t;
```

Для каждого типа есть функция. Выбор функции делается так:

```c
load_func_t get_load_function(load_type_t type) {
    switch (type) {
        case LOAD_CPU:    return cpu_load;
        case LOAD_MEMORY: return memory_load;
        case LOAD_MIXED:  return mixed_load;
        default:          return cpu_load;
    }
}
```

CPU-нагрузка выполняет математические операции:

```c
result += sin(i * 0.001) * cos(i * 0.002);
result += sqrt(fabs(result) + 1.0);
```

Такие операции заставляют процессор активно считать и почти не используют память.

Memory-нагрузка постоянно выделяет, заполняет, читает и освобождает блоки памяти:

```c
char* chunk = malloc(chunk_size);
if (chunk) {
    for (size_t j = 0; j < chunk_size; j++) {
        chunk[j] = (char)(i & 0xFF);
    }
    for (size_t j = 0; j < chunk_size; j++) {
        sum += chunk[j];
    }
    free(chunk);
}
```

Это нагружает аллокатор памяти и подсистему памяти.

Mixed-нагрузка объединяет вычисления и выделение памяти:

```c
result += sin(i * 0.001) * cos(i * 0.002);

char* chunk = malloc(chunk_size);
if (chunk) {
    for (size_t j = 0; j < chunk_size; j++) {
        chunk[j] = (char)(i & 0xFF);
    }
    free(chunk);
}
```

## Привязка к коду: потоки

Один шаг нагрузки запускает несколько потоков:

```c
for (int i = 0; i < num_threads; i++) {
    if (pthread_create(&threads[i], NULL, load_func, &stats[i]) != 0) {
        fprintf(stderr, "ERROR: Failed to create thread %d\n", i);
        g_running = 0;
        break;
    }
}
```

Каждому потоку передается своя структура `thread_stats_t`, где хранится число итераций, результат, время выполнения и флаг активности.

После запуска главный поток ждет завершения рабочих потоков:

```c
for (int i = 0; i < num_threads && g_running; i++) {
    pthread_join(threads[i], NULL);
}
```

`pthread_join` нужен, чтобы корректно дождаться потока и собрать статистику.

Количество потоков увеличивается по шагам:

```c
int current_threads = params.initial_threads;

while (g_running && current_threads <= params.max_threads) {
    run_load_step(current_threads, params.iterations_per_thread, load_func);
    current_threads += params.step_threads;
}
```

## Привязка к коду: сигналы

Программа реагирует на сигналы:

```c
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
    } else if (sig == SIGUSR1) {
        g_pause = !g_pause;
    }
}
```

`Ctrl+C` или `SIGTERM` останавливают тест. `SIGUSR1` переключает паузу:

```bash
kill -USR1 <PID>
```

Внутри рабочих циклов это проверяется так:

```c
if (g_pause) {
    usleep(10000);
    i--;
    continue;
}
```

Итерация уменьшается обратно, чтобы пауза не "съедала" запланированное количество работы.

## Привязка к коду: монитор

Монитор читает CPU-статистику из `/proc/stat`:

```c
static int read_cpu_stats(long* user, long* nice, long* system, long* idle) {
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return -1;

    if (fscanf(f, "cpu %ld %ld %ld %ld", user, nice, system, idle) != 4) {
        fclose(f);
        return -1;
    }
```

Загрузка CPU считается по разнице между двумя замерами:

```c
long total_diff = curr_total - prev_total;
long idle_diff = curr_idle - prev_idle;

return (100.0 * (total_diff - idle_diff)) / total_diff;
```

Память читается из `/proc/meminfo`:

```c
if (strncmp(line, "MemTotal:", 9) == 0) {
    sscanf(line + 9, "%ld", total);
} else if (strncmp(line, "MemFree:", 8) == 0) {
    sscanf(line + 8, "%ld", free);
} else if (strncmp(line, "MemAvailable:", 13) == 0) {
    sscanf(line + 13, "%ld", available);
}
```

Количество процессов считается обходом `/proc` и подсчетом каталогов с числовыми именами:

```c
while ((entry = readdir(proc)) != NULL) {
    if (entry->d_type == DT_DIR) {
        int is_pid = 1;
        for (char* p = entry->d_name; *p; p++) {
            if (*p < '0' || *p > '9') {
                is_pid = 0;
                break;
            }
        }
        if (is_pid) count++;
    }
}
```

## На что обратить внимание

Лабораторная Linux-специфична: `monitor` и часть измерений `stress_test` читают `/proc`, которого нет в таком виде на macOS.

В `monitor.c` есть строка:

```c
printf("\nSYSTEM INFOn");
```

Скорее всего, здесь подразумевалось `"\nSYSTEM INFO\n"`. На логику мониторинга это не влияет, но вывод заголовка будет выглядеть странно.

Стресс-тест нужно запускать аккуратно: большое количество потоков и итераций может сильно нагрузить систему.

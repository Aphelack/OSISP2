# LabWork 3. Просмотр процессов (macOS/Linux)

## Идея лабораторной

Лабораторная реализует упрощенный аналог команды `ps`. Программа выводит список процессов или подробную информацию об одном PID.

Важно: способ получения информации зависит от ОС:

- **Linux**: данные читаются из псевдофайловой системы `/proc` (например, `/proc/<pid>/stat`, `/proc/<pid>/status`).
- **macOS**: данных в формате `/proc` нет, поэтому используется системный API (`libproc` + `sysctl`).

Абстрактно здесь изучаются три вещи:

1. Как операционная система предоставляет данные о процессах через файловый интерфейс.
2. Как C-программа читает и разбирает системные файлы.
3. Как сделать небольшую CLI-утилиту с аргументами командной строки.

## Как собрать и запускать

Из каталога `LabWork_3`:

```bash
make
```

Показать список процессов:

```bash
./myps -l
```

Показать подробную информацию о конкретном процессе:

```bash
./myps -p 1
```

Записать результат в файл:

```bash
./myps -l -o test_output.txt
./myps -p 1 -o pid1.txt
```

Запустить встроенные проверки из `Makefile`:

```bash
make test
```

## Структура кода

Файл `main.c` отвечает за аргументы командной строки, выбор режима и вывод. Файл `proc.c` содержит платформозависимую часть получения информации о процессах (Linux `/proc` или macOS `libproc/sysctl`). Заголовок `proc.h` описывает структуру данных и функции, которые связывают эти файлы.

Главная структура процесса объявлена так:

```c
typedef struct {
    pid_t pid;
    pid_t ppid;
    char name[256];
    char state;
    char state_str[32];
    unsigned long vsize;
    long rss;
    unsigned long utime;
    unsigned long stime;
    int threads;
    uid_t uid;
} proc_info_t;
```

Она хранит PID, PPID, имя процесса, состояние, память, процессорное время, количество потоков и UID владельца.

## Привязка к коду: аргументы командной строки

В `main.c` используется `getopt`:

```c
while ((opt = getopt(argc, argv, "lp:o:")) != -1) {
    switch (opt) {
    case 'l':
        mode = 1;
        break;
    case 'p':
        mode = 2;
        target_pid = atoi(optarg);
        break;
    case 'o':
        outpath = optarg;
        break;
    }
}
```

Строка `"lp:o:"` означает: опция `-l` без значения, опция `-p` со значением, опция `-o` со значением.

Если выбран вывод в файл, программа открывает его через `fopen`:

```c
FILE *out = stdout;
if (outpath) {
    out = fopen(outpath, "w");
    if (!out) {
        perror("fopen");
        return EXIT_FAILURE;
    }
}
```

По умолчанию вывод идет в `stdout`, но при наличии `-o` поток вывода заменяется на файл.

## Привязка к коду: чтение одного процесса

Основная функция для получения информации о процессе — `get_proc_info(pid, &info)`.

### Linux-вариант (через `/proc`)

На Linux функция открывает `/proc/<pid>/stat`, извлекает имя процесса в скобках и парсит поля через `sscanf`, а UID читает из `/proc/<pid>/status`.

```c
int get_proc_info(pid_t pid, proc_info_t *info)
{
    char path[512];
    FILE *fp;

    memset(info, 0, sizeof(*info));
    info->pid = pid;

    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (!fp)
        return -1;
```

Имя процесса в `/proc/<pid>/stat` находится в круглых скобках, поэтому код ищет первую `(` и последнюю `)`:

```c
char *start = strchr(buf, '(');
char *end = strrchr(buf, ')');
if (!start || !end)
    return -1;

size_t name_len = end - start - 1;
strncpy(info->name, start + 1, name_len);
info->name[name_len] = '\0';
```

Это важно, потому что имя процесса может содержать пробелы, и обычное разделение строки по пробелам было бы ненадежным.

Дальше поля после имени разбираются через `sscanf`:

```c
int n = sscanf(end + 2,
    "%c %d %d %d %d %d %u "
    "%lu %lu %lu %lu %lu %lu "
    "%ld %ld %ld %ld %d %ld "
    "%*s %lu %ld",
    &info->state, &info->ppid, ...,
    &info->utime, &info->stime,
    ...,
    &info->threads, ...,
    &info->vsize, &info->rss);
```

Здесь часть полей сохраняется в `info`, а ненужные поля считываются во временные переменные или пропускаются.

UID берется из другого файла:

```c
snprintf(path, sizeof(path), "/proc/%d/status", pid);
fp = fopen(path, "r");
if (fp) {
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            sscanf(line + 4, "%d", &info->uid);
            break;
        }
    }
    fclose(fp);
}
```

Файл `/proc/<pid>/status` удобнее для чтения UID, потому что он представлен в человекочитаемом формате.

### macOS-вариант (через `libproc` + fallback `sysctl`)

На macOS `get_proc_info` в первую очередь использует `proc_pidinfo`:

- `PROC_PIDTBSDINFO` — базовые сведения (PPID, UID, имя, статус).
- `PROC_PIDTASKINFO` — расширенная статистика (память, времена CPU, количество потоков).

Фрагмент логики в `proc.c`:

- сначала пробуем `proc_pidinfo(pid, PROC_PIDTBSDINFO, ...)`
- если не получилось (часто для системных/защищенных процессов), используем запасной путь через `sysctl(KERN_PROC_PID, pid)`

В коде это выглядит так:

```c
ret = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd, sizeof(bsd));
if (ret != (int)sizeof(bsd))
    return get_proc_info_sysctl(pid, info);
```

Почему нужен fallback: в macOS часть информации может быть недоступна из-за прав/ограничений, но `sysctl` часто позволяет хотя бы получить имя процесса, PPID, UID и состояние.

## Привязка к коду: список всех процессов

### Linux-вариант (через `/proc`)

На Linux список процессов получается обходом каталога `/proc`:

```c
dp = opendir("/proc");
...
while ((entry = readdir(dp)) != NULL) {
    int is_pid = 1;
    for (int i = 0; entry->d_name[i]; i++) {
        if (!isdigit((unsigned char)entry->d_name[i])) {
            is_pid = 0;
            break;
        }
    }
```

В `/proc` есть много системных файлов и каталогов, но каталоги с числовыми именами соответствуют процессам. Поэтому программа оставляет только имена, состоящие из цифр.

Для каждого PID вызывается уже знакомая функция:

```c
pid_t pid = atoi(entry->d_name);
proc_info_t info;
if (get_proc_info(pid, &info) == 0) {
    fprintf(out, "%-8d %s\n", info.pid, info.name);
    count++;
}
```

### macOS-вариант (через `proc_listpids`)

На macOS список PID берется через `proc_listpids(PROC_ALL_PIDS, ...)`, после чего для каждого PID вызывается `get_proc_info`:

```c
bytes = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
...
bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, bytes);
...
for (...) {
    if (get_proc_info(pids[i], &info) == 0) {
        fprintf(out, "%-8d %s\n", info.pid, info.name);
    }
}
```

## На что обратить внимание

Эта лабораторная теперь **кроссплатформенная**: под Linux используется `/proc`, а под macOS — `libproc/sysctl`.

Особенности, которые важно понимать:

- **Права доступа**: на macOS `proc_pidinfo` может не вернуть данные для некоторых PID (особенно системных). Поэтому часть полей (например, потоки/память/времена) может быть `0`, если сработал fallback.
- **Единицы измерения времени**: в нашей реализации на macOS `Utime/Stime` выводятся в наносекундах (`ns`), на Linux — в тиках (`ticks`), потому что источники данных разные.
- **Гонки**: процессы могут завершиться прямо во время обхода списка PID (и на Linux, и на macOS). Поэтому ситуация, когда PID был найден, но информацию о нем уже нельзя прочитать, является нормальной — функция вернет `-1`, а процесс будет пропущен.

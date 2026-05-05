#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include "proc.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <libproc.h>
#include <sys/proc.h>
#endif

static const char *state_to_str(char state)
{
    switch (state) {
    case 'R': return "Running";
    case 'S': return "Sleeping";
    case 'D': return "Disk sleep";
    case 'Z': return "Zombie";
    case 'T': return "Stopped";
    case 't': return "Tracing stop";
    case 'X': return "Dead";
    case 'I': return "Idle";
    default:  return "Unknown";
    }
}

#ifdef __APPLE__
static char mac_state_to_char(int state)
{
    switch (state) {
#ifdef SRUN
    case SRUN: return 'R';
#endif
#ifdef SSLEEP
    case SSLEEP: return 'S';
#endif
#ifdef SSTOP
    case SSTOP: return 'T';
#endif
#ifdef SZOMB
    case SZOMB: return 'Z';
#endif
#ifdef SIDL
    case SIDL: return 'I';
#endif
    default: return '?';
    }
}

static int get_proc_info_sysctl(pid_t pid, proc_info_t *info)
{
    struct kinfo_proc kp;
    size_t len = sizeof(kp);
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };

    memset(&kp, 0, sizeof(kp));
    if (sysctl(mib, 4, &kp, &len, NULL, 0) < 0 || len == 0)
        return -1;

    memset(info, 0, sizeof(*info));
    info->pid = pid;
    info->ppid = kp.kp_eproc.e_ppid;
    info->uid = kp.kp_eproc.e_ucred.cr_uid;
    info->state = mac_state_to_char(kp.kp_proc.p_stat);
    snprintf(info->state_str, sizeof(info->state_str), "%s", state_to_str(info->state));
    snprintf(info->name, sizeof(info->name), "%s", kp.kp_proc.p_comm);

    return 0;
}

int get_proc_info(pid_t pid, proc_info_t *info)
{
    struct proc_bsdinfo bsd;
    struct proc_taskinfo task;
    int ret;
    long page_size = sysconf(_SC_PAGESIZE);

    memset(info, 0, sizeof(*info));
    info->pid = pid;

    ret = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd, sizeof(bsd));
    if (ret != (int)sizeof(bsd))
        return get_proc_info_sysctl(pid, info);

    info->ppid = bsd.pbi_ppid;
    info->uid = bsd.pbi_uid;
    info->state = mac_state_to_char(bsd.pbi_status);
    snprintf(info->state_str, sizeof(info->state_str), "%s", state_to_str(info->state));

    if (bsd.pbi_name[0] != '\0') {
        snprintf(info->name, sizeof(info->name), "%s", bsd.pbi_name);
    } else {
        snprintf(info->name, sizeof(info->name), "%d", pid);
    }

    ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task, sizeof(task));
    if (ret == (int)sizeof(task)) {
        info->vsize = (unsigned long)task.pti_virtual_size;
        info->rss = page_size > 0 ? (long)(task.pti_resident_size / (uint64_t)page_size) : 0;
        info->utime = (unsigned long)task.pti_total_user;
        info->stime = (unsigned long)task.pti_total_system;
        info->threads = task.pti_threadnum;
    }

    return 0;
}

int list_all_procs(FILE *out)
{
    int bytes;
    int count = 0;
    pid_t *pids;

    bytes = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    if (bytes <= 0) {
        perror("proc_listpids");
        return -1;
    }

    pids = malloc((size_t)bytes);
    if (!pids) {
        perror("malloc");
        return -1;
    }

    bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, bytes);
    if (bytes <= 0) {
        perror("proc_listpids");
        free(pids);
        return -1;
    }

    fprintf(out, "%-8s %s\n", "PID", "NAME");
    fprintf(out, "-------- ----------------\n");

    int total = bytes / (int)sizeof(pid_t);
    for (int i = 0; i < total; i++) {
        if (pids[i] <= 0)
            continue;

        proc_info_t info;
        if (get_proc_info(pids[i], &info) == 0) {
            fprintf(out, "%-8d %s\n", info.pid, info.name);
            count++;
        }
    }

    free(pids);
    fprintf(out, "\nTotal: %d processes\n", count);
    return count;
}
#else
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

    char buf[2048];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    char *start = strchr(buf, '(');
    char *end = strrchr(buf, ')');
    if (!start || !end)
        return -1;

    size_t name_len = end - start - 1;
    if (name_len >= sizeof(info->name))
        name_len = sizeof(info->name) - 1;
    strncpy(info->name, start + 1, name_len);
    info->name[name_len] = '\0';

    int dummy_int;
    unsigned int dummy_uint;
    unsigned long dummy_ul;
    long dummy_l;

    int n = sscanf(end + 2,
        "%c %d %d %d %d %d %u "
        "%lu %lu %lu %lu %lu %lu "
        "%ld %ld %ld %ld %d %ld "
        "%*s %lu %ld",
        &info->state, &info->ppid, &dummy_int, &dummy_int, &dummy_int,
        &dummy_int, &dummy_uint,
        &dummy_ul, &dummy_ul, &dummy_ul, &dummy_ul,
        &info->utime, &info->stime,
        &dummy_l, &dummy_l, &dummy_l, &dummy_l,
        &info->threads, &dummy_l,
        &info->vsize, &info->rss);

    if (n < 21)
        info->vsize = 0;

    snprintf(info->state_str, sizeof(info->state_str), "%s", state_to_str(info->state));

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

    return 0;
}

int list_all_procs(FILE *out)
{
    DIR *dp;
    struct dirent *entry;
    int count = 0;

    dp = opendir("/proc");
    if (!dp) {
        perror("opendir /proc");
        return -1;
    }

    fprintf(out, "%-8s %s\n", "PID", "NAME");
    fprintf(out, "-------- ----------------\n");

    while ((entry = readdir(dp)) != NULL) {
        /* Каталоги с числовыми именами = процессы */
        int is_pid = 1;
        for (int i = 0; entry->d_name[i]; i++) {
            if (!isdigit((unsigned char)entry->d_name[i])) {
                is_pid = 0;
                break;
            }
        }
        if (!is_pid)
            continue;

        pid_t pid = atoi(entry->d_name);
        proc_info_t info;
        if (get_proc_info(pid, &info) == 0) {
            fprintf(out, "%-8d %s\n", info.pid, info.name);
            count++;
        }
    }

    closedir(dp);
    fprintf(out, "\nTotal: %d processes\n", count);
    return count;
}
#endif

void print_proc_info(const proc_info_t *info, FILE *out)
{
    long page_size = sysconf(_SC_PAGESIZE);

    fprintf(out, "PID:      %d\n", info->pid);
    fprintf(out, "PPID:     %d\n", info->ppid);
    fprintf(out, "Name:     %s\n", info->name);
    fprintf(out, "State:    %c (%s)\n", info->state, info->state_str);
    fprintf(out, "UID:      %d\n", info->uid);
    fprintf(out, "Threads:  %d\n", info->threads);
    fprintf(out, "VmSize:   %lu KB\n", info->vsize / 1024);
    fprintf(out, "RSS:      %ld KB\n", info->rss * page_size / 1024);
#ifdef __APPLE__
    fprintf(out, "Utime:    %lu ns\n", info->utime);
    fprintf(out, "Stime:    %lu ns\n", info->stime);
#else
    fprintf(out, "Utime:    %lu ticks\n", info->utime);
    fprintf(out, "Stime:    %lu ticks\n", info->stime);
#endif
}

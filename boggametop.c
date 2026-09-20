#define _GNU_SOURCE

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdint.h>
#include <limits.h>

#define REFRESH_US 500000
#define BAR_WIDTH  30
#define FAN_COUNT  3

typedef struct {
    unsigned long long total;
    unsigned long long idle;
} cpu_sample_t;

static int read_ull(const char *path, unsigned long long *value) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    int ok = fscanf(f, "%llu", value);
    fclose(f);

    return ok == 1 ? 0 : -1;
}

static int read_text(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    if (!fgets(buf, len, f)) {
        fclose(f);
        return -1;
    }

    fclose(f);

    buf[strcspn(buf, "\r\n")] = 0;
    return 0;
}

static int read_value(const char *path, char *buf, size_t len) {
    return read_text(path, buf, len);
}

static int find_hwmon(const char *wanted, char *out, size_t len) {
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir)
        return -1;

    struct dirent *de;

    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0)
            continue;

        char path[PATH_MAX];
        char name[128];

        snprintf(path, sizeof(path),
                 "/sys/class/hwmon/%s/name",
                 de->d_name);

        if (read_text(path, name, sizeof(name)) == 0 &&
            strcmp(name, wanted) == 0) {

            snprintf(out, len,
                     "/sys/class/hwmon/%s",
                     de->d_name);

            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    return -1;
}

static int find_gpu_device(char *out, size_t len) {
    DIR *dir = opendir("/sys/class/drm");
    if (!dir)
        return -1;

    struct dirent *de;

    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "card", 4) != 0)
            continue;

        /* salta connector tipo card1-HDMI-A-1 */
        if (strchr(de->d_name, '-'))
            continue;

        char path[PATH_MAX];

        snprintf(path, sizeof(path),
                 "/sys/class/drm/%s/device/gpu_busy_percent",
                 de->d_name);

        if (access(path, R_OK) == 0) {
            snprintf(out, len,
                     "/sys/class/drm/%s/device",
                     de->d_name);

            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    return -1;
}

static int read_cpu(cpu_sample_t *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return -1;

    char line[512];

    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }

    fclose(f);

    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;

    int n = sscanf(
        line,
        "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &user,
        &nice,
        &system,
        &idle,
        &iowait,
        &irq,
        &softirq,
        &steal
    );

    if (n < 4)
        return -1;

    s->idle =
        idle +
        (n >= 5 ? iowait : 0);

    s->total =
        user +
        nice +
        system +
        idle +
        (n >= 5 ? iowait : 0) +
        (n >= 6 ? irq : 0) +
        (n >= 7 ? softirq : 0) +
        (n >= 8 ? steal : 0);

    return 0;
}

static double cpu_usage(cpu_sample_t *a, cpu_sample_t *b) {
    unsigned long long total = b->total - a->total;
    unsigned long long idle  = b->idle  - a->idle;

    if (!total)
        return 0.0;

    return 100.0 *
           (double)(total - idle) /
           (double)total;
}

static int read_memory(unsigned long long *total_kb,
                       unsigned long long *available_kb) {

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1;

    char key[64];
    unsigned long long val;
    char unit[32];

    *total_kb = 0;
    *available_kb = 0;

    while (fscanf(f, "%63s %llu %31s",
                  key, &val, unit) == 3) {

        if (!strcmp(key, "MemTotal:"))
            *total_kb = val;

        else if (!strcmp(key, "MemAvailable:"))
            *available_kb = val;
    }

    fclose(f);

    return (*total_kb && *available_kb) ? 0 : -1;
}

static double temp_from_hwmon(const char *hwmon) {
    char path[PATH_MAX];
    unsigned long long milli;

    snprintf(path, sizeof(path),
             "%s/temp1_input", hwmon);

    if (read_ull(path, &milli) != 0)
        return -1.0;

    return milli / 1000.0;
}

static double gib(unsigned long long bytes) {
    return (double)bytes /
           1024.0 /
           1024.0 /
           1024.0;
}

static void draw_bar(int row,
                     int col,
                     double percent) {

    int filled =
        (int)(percent / 100.0 * BAR_WIDTH);

    if (filled < 0)
        filled = 0;

    if (filled > BAR_WIDTH)
        filled = BAR_WIDTH;

    mvprintw(row, col, "[");

    for (int i = 0; i < BAR_WIDTH; i++) {
        addch(i < filled ? '#' : ' ');
    }

    addch(']');
}

static void read_fans(char rpm[FAN_COUNT][32],
                      char mode[FAN_COUNT][32],
                      char level[FAN_COUNT][32],
                      char up[FAN_COUNT][64],
                      char down[FAN_COUNT][64]) {

    for (int i = 0; i < FAN_COUNT; i++) {
        char path[PATH_MAX];
        int fan = i + 1;

        snprintf(path, sizeof(path),
                 "/sys/class/ec_su_axb35/fan%d/rpm",
                 fan);

        if (read_value(path,
                       rpm[i],
                       sizeof(rpm[i])) != 0)
            strcpy(rpm[i], "N/A");

        snprintf(path, sizeof(path),
                 "/sys/class/ec_su_axb35/fan%d/mode",
                 fan);

        if (read_value(path,
                       mode[i],
                       sizeof(mode[i])) != 0)
            strcpy(mode[i], "N/A");

        snprintf(path, sizeof(path),
                 "/sys/class/ec_su_axb35/fan%d/level",
                 fan);

        if (read_value(path,
                       level[i],
                       sizeof(level[i])) != 0)
            strcpy(level[i], "N/A");

        snprintf(path, sizeof(path),
                 "/sys/class/ec_su_axb35/fan%d/rampup_curve",
                 fan);

        if (read_value(path,
                       up[i],
                       sizeof(up[i])) != 0)
            strcpy(up[i], "N/A");

        snprintf(path, sizeof(path),
                 "/sys/class/ec_su_axb35/fan%d/rampdown_curve",
                 fan);

        if (read_value(path,
                       down[i],
                       sizeof(down[i])) != 0)
            strcpy(down[i], "N/A");
    }
}

int main(void) {
    char gpu[PATH_MAX] = "";
    char cpu_hwmon[PATH_MAX] = "";
    char gpu_hwmon[PATH_MAX] = "";
    char nvme_hwmon[PATH_MAX] = "";

    find_gpu_device(gpu, sizeof(gpu));
    find_hwmon("k10temp",
               cpu_hwmon,
               sizeof(cpu_hwmon));

    find_hwmon("amdgpu",
               gpu_hwmon,
               sizeof(gpu_hwmon));

    find_hwmon("nvme",
               nvme_hwmon,
               sizeof(nvme_hwmon));

    cpu_sample_t prev_cpu = {0};
    cpu_sample_t now_cpu  = {0};

    read_cpu(&prev_cpu);

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    timeout(0);

    while (1) {
        usleep(REFRESH_US);

        int ch = getch();

        if (ch == 'q' || ch == 'Q')
            break;

        erase();

        /* CPU */
        read_cpu(&now_cpu);

        double cpu_pct =
            cpu_usage(&prev_cpu, &now_cpu);

        prev_cpu = now_cpu;

        /* System memory */
        unsigned long long mem_total = 0;
        unsigned long long mem_avail = 0;

        read_memory(&mem_total, &mem_avail);

        double mem_used_gib =
            (mem_total - mem_avail) /
            1024.0 /
            1024.0;

        double mem_total_gib =
            mem_total /
            1024.0 /
            1024.0;

        /* GPU */
        unsigned long long gpu_busy = 0;

        unsigned long long vram_used = 0;
        unsigned long long vram_total = 0;

        unsigned long long gtt_used = 0;
        unsigned long long gtt_total = 0;

        if (*gpu) {
            char path[PATH_MAX];

            snprintf(path, sizeof(path),
                     "%s/gpu_busy_percent",
                     gpu);

            read_ull(path, &gpu_busy);

            snprintf(path, sizeof(path),
                     "%s/mem_info_vram_used",
                     gpu);

            read_ull(path, &vram_used);

            snprintf(path, sizeof(path),
                     "%s/mem_info_vram_total",
                     gpu);

            read_ull(path, &vram_total);

            snprintf(path, sizeof(path),
                     "%s/mem_info_gtt_used",
                     gpu);

            read_ull(path, &gtt_used);

            snprintf(path, sizeof(path),
                     "%s/mem_info_gtt_total",
                     gpu);

            read_ull(path, &gtt_total);
        }

        /* Temperatures */
        double cpu_temp =
            *cpu_hwmon ?
            temp_from_hwmon(cpu_hwmon) :
            -1.0;

        double gpu_temp =
            *gpu_hwmon ?
            temp_from_hwmon(gpu_hwmon) :
            -1.0;

        double nvme_temp =
            *nvme_hwmon ?
            temp_from_hwmon(nvme_hwmon) :
            -1.0;

        /* Fans */
        char fan_rpm[FAN_COUNT][32];
        char fan_mode[FAN_COUNT][32];
        char fan_level[FAN_COUNT][32];
        char fan_up[FAN_COUNT][64];
        char fan_down[FAN_COUNT][64];

        read_fans(
            fan_rpm,
            fan_mode,
            fan_level,
            fan_up,
            fan_down
        );

        /*
         * Drawing
         */

        mvprintw(0, 2,
                 "BOSGAME AI395 MONITOR");

        mvprintw(2, 2, "CPU");

        draw_bar(
            2,
            8,
            cpu_pct
        );

        mvprintw(
            2,
            42,
            "%6.1f%%",
            cpu_pct
        );

        if (cpu_temp >= 0) {
            mvprintw(
                2,
                52,
                "%5.1f C",
                cpu_temp
            );
        }

        mvprintw(4, 2, "GPU");

        draw_bar(
            4,
            8,
            (double)gpu_busy
        );

        mvprintw(
            4,
            42,
            "%6llu%%",
            gpu_busy
        );

        if (gpu_temp >= 0) {
            mvprintw(
                4,
                52,
                "%5.1f C",
                gpu_temp
            );
        }

        mvprintw(6, 2, "MEMORY");

        mvprintw(
            7,
            4,
            "System : %6.1f / %6.1f GiB",
            mem_used_gib,
            mem_total_gib
        );

        mvprintw(
            8,
            4,
            "VRAM   : %6.1f / %6.1f GiB",
            gib(vram_used),
            gib(vram_total)
        );

        mvprintw(
            9,
            4,
            "GTT    : %6.1f / %6.1f GiB",
            gib(gtt_used),
            gib(gtt_total)
        );

        mvprintw(11, 2, "TEMPERATURES");

        if (cpu_temp >= 0) {
            mvprintw(
                12,
                4,
                "CPU Tctl : %5.1f C",
                cpu_temp
            );
        }

        if (gpu_temp >= 0) {
            mvprintw(
                13,
                4,
                "GPU edge : %5.1f C",
                gpu_temp
            );
        }

        if (nvme_temp >= 0) {
            mvprintw(
                14,
                4,
                "NVMe     : %5.1f C",
                nvme_temp
            );
        }

        /*
         * Fans
         */

        mvprintw(16, 2, "FANS");

        mvprintw(17, 12, "%-18s", "FAN1");
        mvprintw(17, 32, "%-18s", "FAN2");
        mvprintw(17, 52, "%-18s", "FAN3");

        mvprintw(18, 2, "RPM");

        for (int i = 0; i < FAN_COUNT; i++) {
            mvprintw(
                18,
                12 + i * 20,
                "%-18s",
                fan_rpm[i]
            );
        }

        mvprintw(19, 2, "Level");

        for (int i = 0; i < FAN_COUNT; i++) {
            mvprintw(
                19,
                12 + i * 20,
                "%-18s",
                fan_level[i]
            );
        }

        mvprintw(20, 2, "Mode");

        for (int i = 0; i < FAN_COUNT; i++) {
            mvprintw(
                20,
                12 + i * 20,
                "%-18s",
                fan_mode[i]
            );
        }

        mvprintw(21, 2, "Up");

        for (int i = 0; i < FAN_COUNT; i++) {
            mvprintw(
                21,
                12 + i * 20,
                "%-18s",
                fan_up[i]
            );
        }

        mvprintw(22, 2, "Down");

        for (int i = 0; i < FAN_COUNT; i++) {
            mvprintw(
                22,
                12 + i * 20,
                "%-18s",
                fan_down[i]
            );
        }

        mvprintw(
            24,
            2,
            "Refresh: %.1fs   q: quit",
            REFRESH_US / 1000000.0
        );

        refresh();
    }

    endwin();
    return 0;
}
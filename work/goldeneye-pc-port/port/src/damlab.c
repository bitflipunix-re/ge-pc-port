#include "damlab.h"

#if defined(DAM_ONLY_LAB)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


static DamLabSnapshot g_dam;
static FILE *g_log;
static double g_last_host_time;
static unsigned long long g_last_proc_ticks;
static int g_last_room = -9999;
static int g_last_camera = -9999;
static float g_last_x, g_last_y, g_last_z;
static int g_have_last_pos;
static uint64_t g_game_ticks;

static float damAbs(float x)
{
    return x < 0.0f ? -x : x;
}

static int damFinite(float x)
{
    return x == x && x < 3.4e38f && x > -3.4e38f;
}

static double damNow(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static unsigned long long damReadProcTicks(void)
{
#if defined(__linux__)
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return 0;

    char buf[4096];
    unsigned long long ticks = 0;
    if (fgets(buf, sizeof(buf), f)) {
        char *p = strrchr(buf, ')');
        if (p && p[1] == ' ') {
            /* After comm): field 3 starts here. utime/stime are fields 14/15,
             * therefore tokens 12/13 from the state token. */
            char *save = NULL;
            char *tok = strtok_r(p + 2, " ", &save);
            int field = 3;
            unsigned long long utime = 0, stime = 0;
            while (tok) {
                if (field == 14) utime = strtoull(tok, NULL, 10);
                if (field == 15) { stime = strtoull(tok, NULL, 10); break; }
                tok = strtok_r(NULL, " ", &save);
                field++;
            }
            ticks = utime + stime;
        }
    }
    fclose(f);
    return ticks;
#else
    return 0;
#endif
}

static void damReadMemory(void)
{
#if defined(__linux__)
    long page = sysconf(_SC_PAGESIZE);
    FILE *f = fopen("/proc/self/statm", "r");
    unsigned long total_pages = 0, rss_pages = 0;
    if (f) {
        if (fscanf(f, "%lu %lu", &total_pages, &rss_pages) == 2 && page > 0)
            g_dam.rss_kb = (rss_pages * (unsigned long)page) / 1024ul;
        fclose(f);
    }

    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64];
        unsigned long value;
        char unit[32];
        while (fscanf(f, "%63s %lu %31s", key, &value, unit) == 3) {
            if (strcmp(key, "MemAvailable:") == 0) {
                g_dam.mem_available_kb = value;
                break;
            }
        }
        fclose(f);
    }
#endif
}

static void damLog(const char *kind)
{
#if defined(DAM_SHOWCASE)
    (void)kind;
    return;
#else
    if (!g_log)
        return;

    fprintf(g_log,
        "DAMLAB kind=%s seq=%llu stage=%d cammode=%d room=%d spawn=%d "
        "pos=%.3f,%.3f,%.3f cam=%.3f,%.3f,%.3f "
        "stan=%p stanh=%.3f fps=%.2f cpu=%.1f rss_kb=%lu avail_kb=%lu flags=0x%X\n",
        kind,
        (unsigned long long)g_dam.sample_seq,
        g_dam.stage, g_dam.camera_mode, g_dam.room, g_dam.spawn_index,
        g_dam.pos_x, g_dam.pos_y, g_dam.pos_z,
        g_dam.cam_x, g_dam.cam_y, g_dam.cam_z,
        (void *)g_dam.stan, g_dam.stan_height,
        g_dam.fps, g_dam.cpu_percent, g_dam.rss_kb,
        g_dam.mem_available_kb, g_dam.anomaly_flags);
    fflush(g_log);
#endif
}

void damLabInit(void)
{
    memset(&g_dam, 0, sizeof(g_dam));
    g_dam.room = -1;
    g_dam.spawn_index = -1;
    g_dam.stage = -1;
#if defined(DAM_SHOWCASE)
    g_log = NULL;
#else
    g_log = fopen("damlab.log", "w");
    if (g_log) {
        fprintf(g_log,
            "DAMLAB_BEGIN oracle_stage=%d oracle_spawn=33 "
            "oracle_pos=4719.000,-18.000,3949.000 "
            "oracle_look=-1.000000,0.000000,-0.000643 oracle_plink=p6g1\n",
            9);
        fflush(g_log);
    }
#endif
    g_last_host_time = damNow();
    g_last_proc_ticks = damReadProcTicks();
}

void damLabRecordSpawn(int spawn_index, float x, float y, float z,
                       float lx, float ly, float lz, uintptr_t stan)
{
    g_dam.spawn_index = spawn_index;
    g_dam.pos_x = x; g_dam.pos_y = y; g_dam.pos_z = z;
    g_dam.stan = stan;

#if !defined(DAM_SHOWCASE)
    if (spawn_index != 33 ||
        damAbs(x - 4719.0f) > 0.01f ||
        damAbs(y - (-18.0f)) > 0.01f ||
        damAbs(z - 3949.0f) > 0.01f ||
        damAbs(lx - (-1.0f)) > 0.01f ||
        damAbs(ly) > 0.01f ||
        damAbs(lz - (-0.000643f)) > 0.01f) {
        g_dam.anomaly_flags |= DAMLAB_ANOM_SPAWN;
    }
#else
    (void)lx; (void)ly; (void)lz;
#endif
    if (!stan)
        g_dam.anomaly_flags |= DAMLAB_ANOM_STAN_NULL;

    g_dam.sample_seq++;
    damLog("SPAWN");
}

void damLabRecordAppliedSpawn(int spawn_index,
                              float request_x, float request_y, float request_z,
                              float collision_x, float collision_y, float collision_z,
                              float view_x, float view_y, float view_z,
                              uintptr_t requested_stan, uintptr_t applied_stan)
{
    g_dam.spawn_index = spawn_index;
    g_dam.pos_x = collision_x;
    g_dam.pos_y = collision_y;
    g_dam.pos_z = collision_z;
    g_dam.cam_x = view_x;
    g_dam.cam_y = view_y;
    g_dam.cam_z = view_z;
    g_dam.stan = applied_stan;

    if (damAbs(collision_x - request_x) > 0.01f ||
        damAbs(collision_y - request_y) > 0.01f ||
        damAbs(collision_z - request_z) > 0.01f ||
        damAbs(view_x - request_x) > 0.01f ||
        damAbs(view_y - request_y) > 0.01f ||
        damAbs(view_z - request_z) > 0.01f ||
        requested_stan != applied_stan) {
        g_dam.anomaly_flags |= DAMLAB_ANOM_APPLY;
    }

    if (!applied_stan)
        g_dam.anomaly_flags |= DAMLAB_ANOM_STAN_NULL;

    g_dam.sample_seq++;
#if !defined(DAM_SHOWCASE)
    if (g_log) {
        fprintf(g_log,
            "DAMLAB kind=APPLY seq=%llu spawn=%d "
            "request=%.3f,%.3f,%.3f collision=%.3f,%.3f,%.3f "
            "view=%.3f,%.3f,%.3f stan_request=%p stan_applied=%p flags=0x%X\n",
            (unsigned long long)g_dam.sample_seq, spawn_index,
            request_x, request_y, request_z,
            collision_x, collision_y, collision_z,
            view_x, view_y, view_z,
            (void *)requested_stan, (void *)applied_stan,
            g_dam.anomaly_flags);
        fflush(g_log);
    }
#endif
}

void damLabGameplayTick(int stage, int camera_mode, int room,
                        float pos_x, float pos_y, float pos_z,
                        float cam_x, float cam_y, float cam_z,
                        float stan_height, uintptr_t stan)
{
    g_game_ticks++;
    unsigned old_flags = g_dam.anomaly_flags;

    g_dam.stage = stage;
    g_dam.camera_mode = camera_mode;
    g_dam.room = room;
    g_dam.pos_x = pos_x;
    g_dam.pos_y = pos_y;
    g_dam.pos_z = pos_z;
    g_dam.cam_x = cam_x;
    g_dam.cam_y = cam_y;
    g_dam.cam_z = cam_z;
    g_dam.stan_height = stan_height;
    g_dam.stan = stan;

    if (!damFinite(pos_x) || !damFinite(pos_y) || !damFinite(pos_z))
        g_dam.anomaly_flags |= DAMLAB_ANOM_POS_NAN;
    if (!stan)
        g_dam.anomaly_flags |= DAMLAB_ANOM_STAN_NULL;
    if (room < 0 || room >= 139)
        g_dam.anomaly_flags |= DAMLAB_ANOM_ROOM;

    int event = 0;
    if (g_have_last_pos) {
        float dx = pos_x - g_last_x;
        float dy = pos_y - g_last_y;
        float dz = pos_z - g_last_z;
        if (dx*dx + dy*dy + dz*dz > 250000.0f) {
            g_dam.anomaly_flags |= DAMLAB_ANOM_POS_JUMP;
            event = 1;
        }
    }
    if (room != g_last_room || camera_mode != g_last_camera)
        event = 1;
    if (g_dam.anomaly_flags != old_flags)
        event = 1;

    g_last_x = pos_x; g_last_y = pos_y; g_last_z = pos_z;
    g_last_room = room;
    g_last_camera = camera_mode;
    g_have_last_pos = 1;

    if ((g_game_ticks % 60u) == 0u || event) {
        g_dam.sample_seq++;
        damLog(event ? "EVENT" : "TICK");
        g_dam.anomaly_flags &= ~(DAMLAB_ANOM_POS_JUMP);
    }
}

void damLabHostSample(float fps)
{
    g_dam.fps = fps;
    double now = damNow();
    unsigned long long ticks = damReadProcTicks();
    long hz = sysconf(_SC_CLK_TCK);

    if (g_last_host_time > 0.0 && now > g_last_host_time && hz > 0 &&
        ticks >= g_last_proc_ticks) {
        g_dam.cpu_percent =
            (float)(((double)(ticks - g_last_proc_ticks) / (double)hz) /
                    (now - g_last_host_time) * 100.0);
    }

    g_last_host_time = now;
    g_last_proc_ticks = ticks;
    damReadMemory();
}

const DamLabSnapshot *damLabGetSnapshot(void)
{
    return &g_dam;
}

void damLabFormatOverlay(char *dst, unsigned dst_size)
{
    if (!dst || !dst_size) return;
#if defined(DAM_SHOWCASE)
    snprintf(dst, dst_size,
        "DAM SHOWCASE\nFPS %03d",
        (int)(g_dam.fps + 0.5f));
#else
    snprintf(dst, dst_size,
        "DAM LAB\nFPS %03d CPU %03d\nRAM %04luM\nPOS %d %d %d\nROOM %03d STAN %s\nFLAGS %02X",
        (int)(g_dam.fps + 0.5f),
        (int)(g_dam.cpu_percent + 0.5f),
        g_dam.rss_kb / 1024ul,
        (int)g_dam.pos_x, (int)g_dam.pos_y, (int)g_dam.pos_z,
        g_dam.room, g_dam.stan ? "YES" : "NO",
        g_dam.anomaly_flags & 0xffu);
#endif
}

void damLabShutdown(void)
{
#if defined(DAM_SHOWCASE)
    g_log = NULL;
#else
    if (g_log) {
        damLog("END");
        fclose(g_log);
        g_log = NULL;
    }
#endif
}

#else

void damLabInit(void) {}
void damLabGameplayTick(int a,int b,int r,float x,float y,float z,float cx,float cy,float cz,float sh,uintptr_t s)
{ (void)a;(void)b;(void)r;(void)x;(void)y;(void)z;(void)cx;(void)cy;(void)cz;(void)sh;(void)s; }
void damLabRecordSpawn(int i,float x,float y,float z,float lx,float ly,float lz,uintptr_t s)
{ (void)i;(void)x;(void)y;(void)z;(void)lx;(void)ly;(void)lz;(void)s; }
void damLabRecordAppliedSpawn(int i,float rx,float ry,float rz,float cx,float cy,float cz,float vx,float vy,float vz,uintptr_t rs,uintptr_t as)
{ (void)i;(void)rx;(void)ry;(void)rz;(void)cx;(void)cy;(void)cz;(void)vx;(void)vy;(void)vz;(void)rs;(void)as; }
void damLabHostSample(float fps) { (void)fps; }
const DamLabSnapshot *damLabGetSnapshot(void) { static DamLabSnapshot s; return &s; }
void damLabFormatOverlay(char *dst, unsigned n) { if (dst && n) dst[0]=0; }
void damLabShutdown(void) {}

#endif

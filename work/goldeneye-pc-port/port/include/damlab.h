#ifndef GE_DAMLAB_H
#define GE_DAMLAB_H

#include <PR/ultratypes.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DamLabSnapshot {
    uint64_t sample_seq;
    int stage;
    int camera_mode;
    int room;
    int spawn_index;
    float pos_x, pos_y, pos_z;
    float cam_x, cam_y, cam_z;
    float stan_height;
    uintptr_t stan;
    float fps;
    float cpu_percent;
    unsigned long rss_kb;
    unsigned long mem_available_kb;
    unsigned anomaly_flags;
} DamLabSnapshot;

enum {
    DAMLAB_ANOM_SPAWN      = 1u << 0,
    DAMLAB_ANOM_STAN_NULL  = 1u << 1,
    DAMLAB_ANOM_POS_NAN    = 1u << 2,
    DAMLAB_ANOM_POS_JUMP   = 1u << 3,
    DAMLAB_ANOM_ROOM       = 1u << 4,
    DAMLAB_ANOM_APPLY      = 1u << 5,
};

void damLabInit(void);
void damLabGameplayTick(int stage, int camera_mode, int room,
                        float pos_x, float pos_y, float pos_z,
                        float cam_x, float cam_y, float cam_z,
                        float stan_height, uintptr_t stan);
void damLabRecordSpawn(int spawn_index, float x, float y, float z,
                       float lx, float ly, float lz, uintptr_t stan);
void damLabRecordAppliedSpawn(int spawn_index,
                              float request_x, float request_y, float request_z,
                              float collision_x, float collision_y, float collision_z,
                              float view_x, float view_y, float view_z,
                              uintptr_t requested_stan, uintptr_t applied_stan);
void damLabHostSample(float fps);
const DamLabSnapshot *damLabGetSnapshot(void);
void damLabFormatOverlay(char *dst, unsigned dst_size);
void damLabShutdown(void);

#ifdef __cplusplus
}
#endif
#endif

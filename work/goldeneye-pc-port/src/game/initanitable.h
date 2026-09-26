#ifndef _INITANITABLE_H_
#define _INITANITABLE_H_
#include <ultra64.h>
#include <assets/animationtable_data.h>



/**
 * Struct to hold animation data. This is never instantiated.
 * Instead, only a pointer to this will exist.
 */
struct animation_table_data {
    /**
     * Array length is arbitrary and shouldn't matter. The largest offset
     * into this is for the last animation pointer 0xE7C0, so just choosing
     * a value bigger than that, like u16_max_value.
    */
    u8 data[0xffff];
};

/**
 * Data holder for animations.
 */
extern struct animation_table_data* ptr_animation_table;

/*
 * Animation records are addressed by 32-bit byte offsets inside the runtime
 * animation blob. Keep that token 32-bit, but never truncate the host base.
 * The non-PORT definitions preprocess to the original N64 expressions.
 */
#ifdef PORT
#include <stdint.h>
#define GE_ANIMDATA_OFFSET(name) ((u32)PTR_ANIM_##name)
#define GE_ANIMDATA_BASE ((uintptr_t)&ptr_animation_table->data)
#else
#define GE_ANIMDATA_OFFSET(name) ((s32)&ANIM_DATA_##name)
#define GE_ANIMDATA_BASE ((s32)&ptr_animation_table->data)
#endif
#define GE_ANIMDATA_ADDR(name) (GE_ANIMDATA_BASE + GE_ANIMDATA_OFFSET(name))
#define GE_ANIMDATA_PTR(name) ((void *)GE_ANIMDATA_ADDR(name))

/**
 * Contains offsets into ptr_animation_table for player and guard animations.
 * The index of each value corresponds to `enum ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_idle (same as ANIM_DATA_idle)
*/
extern s32 animation_table_ptrs1[];

/**
 * Contains offsets into ptr_animation_table for object/vehicle animations.
 * The index of each value corresponds to `enum AIRCRAFT_ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_helicopter_cradle (same as ANIM_DATA_helicopter_cradle)
 *
 * D32/D33: s32 offsets (N64 layout — 4-byte elements on both targets);
 * cast to ModelAnimation * at the use sites.
*/
extern s32 animation_table_ptrs2[];

#endif

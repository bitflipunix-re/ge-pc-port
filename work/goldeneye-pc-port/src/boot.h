#ifndef _BOOT_H_
#define _BOOT_H_
#include <ultra64.h>

void boot(void);
#ifdef PORT
void *get_csegmentSegmentStart(void);
void *get_cdataSegmentRomStart(void);
void *get_cdataSegmentRomEnd(void);
void *get_inflateSegmentRomStart(void);
void *get_inflateSegmentRomEnd(void);
u32 jump_decompressfile(void *source, void *target, void *buffer);
#else
u32 get_csegmentSegmentStart(void);
u32 get_cdataSegmentRomStart(void);
u32 get_cdataSegmentRomEnd(void);
u32 get_inflateSegmentRomStart(void);
u32 get_inflateSegmentRomEnd(void);
u32 jump_decompressfile(u32 source, u32 target, u32 buffer);
#endif

#endif

#ifndef GFX_PC_H
#define GFX_PC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unordered_map>
#include <list>
#include <cstddef>

#include <PR/gbi.h>

#include "system.h"

#define SCREEN_WIDTH ((int32_t)gfx_current_native_viewport.width)
#define SCREEN_HEIGHT ((int32_t)gfx_current_native_viewport.height)

extern uintptr_t gfxFramebuffer;

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

struct TextureCacheKey {
    const uint8_t* texture_addr;
    const uint8_t* palette_addrs[2];
    uint8_t fmt, siz;
    uint8_t palette_index;
    // D74: the same source address can be loaded with different sizes
    // (gDPLoadBlock mip chains vs partial loads); include the size so a
    // truncated first import cannot poison later full imports.
    uint32_t size_bytes;
    // D217: for CI (palettized) textures, a hash of the decoded rdp.palette
    // content at import time. GE assembles weapon / character model DLs in
    // scratch arena RAM and reissues gDPLoadTLUT with different palette
    // *content* from a repeated source address, so
    // {texture_addr, palette_addrs, palette_index, size_bytes} alone collides
    // across materials/frames -> a CI tile takes a stale cache HIT decoded
    // against a different palette. Recomputed only in gfx_dp_load_tlut(), so
    // there is no per-texel or per-draw cost. Zero for non-CI textures.
    uint32_t palette_hash;

    bool operator==(const TextureCacheKey&) const noexcept = default;

    struct Hasher {
        size_t operator()(const TextureCacheKey& key) const noexcept {
            /* The old hash used only texture_addr, so every format/size/palette
             * variant sharing an arena address landed in one bucket. GE does
             * exactly that for dynamic model/weapon textures. Mix the complete
             * key so ordinary lookups stay close to O(1) on the ARM cores. */
            size_t h = (size_t)((uintptr_t)key.texture_addr >> 4);
            auto mix = [&h](size_t v) noexcept {
                h ^= v + (size_t)0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            };
            mix((size_t)((uintptr_t)key.palette_addrs[0] >> 4));
            mix((size_t)((uintptr_t)key.palette_addrs[1] >> 4));
            mix((size_t)key.fmt | ((size_t)key.siz << 8) | ((size_t)key.palette_index << 16));
            mix((size_t)key.size_bytes);
            mix((size_t)key.palette_hash);
            return h;
        }
    };
};

typedef std::unordered_map<TextureCacheKey, struct TextureCacheValue, TextureCacheKey::Hasher> TextureCacheMap;
typedef std::pair<const TextureCacheKey, struct TextureCacheValue> TextureCacheNode;

struct TextureCacheValue {
    uint32_t texture_id;
    uint8_t cms, cmt;
    bool linear_filter;

    std::list<struct TextureCacheMapIter>::iterator lru_location;
};

struct TextureCacheMapIter {
    TextureCacheMap::iterator it;
};

extern "C" {

#include "gfx_api.h"

}

#endif

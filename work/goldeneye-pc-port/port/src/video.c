/*
 * Video: SDL2 window + OpenGL context + frame pacing on top of fast3d.
 *
 * The window itself lives in port/fast3d/gfx_sdl2.cpp (the wapi backend);
 * this file wires the rendering API up, owns frame boundaries and FPS stats,
 * and exposes the small surface the libultra VI shims need.
 *
 * Modelled on the PD port's port/src/video.c (slimmed: no options menu).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define GE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define GE_MKDIR(p) mkdir(p, 0777)
/* Local host-libc declaration; see port/shim/stdlib.h. */
extern void exit(int status);
#endif

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "damlab.h"
#include "input.h"
#include "optionsoverlay.h"
#include "../include/crash.h"

#if defined(__aarch64__)
#include <GLES2/gl2.h>
#endif

#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_sdl.h"
#include "../fast3d/gfx_opengl.h"

/* GE's internal resolution: NTSC LAN1 is 640x480; PAL LAN1 shows a
 * 640x400 area. The window opens at the native size (1:1) by default. */
#ifdef REFRESH_PAL
#define GE_NATIVE_W 640
#define GE_NATIVE_H 400
#else
#define GE_NATIVE_W 640
#define GE_NATIVE_H 480
#endif

static struct GfxWindowManagerAPI *wmAPI;
static struct GfxRenderingAPI *renderingAPI;
static int initDone = 0;
static int crashScreenActive = 0;

/* Dam lab hardware/runtime HUD. Keep diagnostics available from F10 but
 * default the intrusive panel off for public alpha builds; telemetry/logging
 * remains live regardless of visibility. */
static int cfgPerfHud = 0;

/*
 * [Video] ge007.ini knobs. Every default reproduces the previously-hardcoded
 * behaviour, so a fresh config or a missing [Video] section changes nothing.
 */
static int cfgVSync         = 1;   /* swap interval: 0 = off, 1 = on            */
static int cfgFpsCap        = 0;   /* frame cap in fps; 0 = uncapped (vsync)    */
static int cfgMSAA          = 4;   /* 1/2/4/8 samples; default 4 (modern ports ship AA on; snaps down to the highest supported level) */
static int cfgTexFilter     = 1;   /* 0 = nearest, 1 = bilinear (default), 2 = N64 3-point */
static int cfgMipmapFilter  = 3;   /* 0 off, 1 nearest, 2 trilinear, 3 auto (legacy behavior) */
static int cfgFramebufferEffects = 1; /* fast3d render-target effects; restart-bound */
static int cfgFixMipTex     = 1;   /* RC2: clip mip-contaminated texture uploads to base height */
static int cfgWrapFix       = 0;   /* D74 sub-tile UV pre-wrap + RC3/D167 non-PoT mask-period wrap (opt-in; GE_WRAPFIX env overrides) */
static int cfgFovScale      = 100; /* D211: percent of the original vertical FOV; 100 = unchanged (byte-identical) */
static int cfgDrawDistance      = 150; /* D218: percent of the level's authored far-clip/fog distance. Default raised 100->150 for v0.2.0: at the authored N64 distance, props visibly fade in just before they become visible on modern displays (Dam alarms / wall switches); 150 is the value the Steam Deck preset playtest-validated. 100 = unchanged N64. */
static int cfgDrawDistanceAutoFov = 1;   /* D218: couple draw distance to Video.FovScale unless DrawDistance is set explicitly */
static int cfgLodDistance         = 150; /* D249: percent scale on the geometry/model LOD-swap distance. Default raised 100->150 for v0.2.0 (same pop-in family as DrawDistance: LOD-swapped props like Dam's alarms/wall switches faded in at range); 150 matches the Steam Deck preset. 100 = unchanged N64. */
static int cfgLodDistanceAutoFov  = 0;   /* off by default -- unlike DrawDistance, this is meant as a standalone perf lever, not something that should silently get more expensive as FovScale widens */
static int cfgAniso         = 4;   /* D212: anisotropic filtering samples; 4 = the value fast3d already applied (no visual delta at default) */
static int cfgFullscreen    = 0;   /* 0 = windowed, 1 = borderless fullscreen   */

/*
 * [Window] persistence. W/H = 0 -> auto (gfx_sdl2 fits a 4:3 window into ~85%
 * of the desktop); X/Y = -1 -> let SDL centre the window.
 * videoSaveWindowState() writes the live geometry back into these on a clean
 * exit (see main.c's atexit handler), so after the first run the file pins
 * whatever size you left it at.
 */
static int cfgWinW   = 0;
static int cfgWinH   = 0;
static int cfgWinX   = -1;
static int cfgWinY   = -1;
static int cfgWinMax = 0;

/*
 * [Game] gameplay-cosmetic knobs (route-(b) hooks in src/, findings D181).
 * portScreenShakeScale multiplies every viShake() amplitude (src/fr.c).
 * 1.0f = original behaviour (headless golden dumps unaffected).
 */
f32 portScreenShakeScale = 1.0f;

/* D216: Game.SkipIntro — read once in src/game/lv.c at title-stage load.
 * 0 (default) = the legal screen + logo attract sequence plays as normal. */
s32 portSkipIntro = 0;

/* D232: Game.NoHitFlash — the community "no damage flash" toggle (route-(b)
 * hook in src/game/bondview2.c currentPlayerSetFadeColour). 0 (default) =
 * original damage flash. */
s32 portNoHitFlash = 0;

/* D257: Game.AllUnlocked — everything-unlocked goodie, OFF by default
 * (faithful N64 progression: levels unlock as you complete them). Consumed
 * once at startup by main.c, which sets the game's own RAM unlock flags
 * (debug_enable_all_levels_flag / debug_007_unlock_flag in
 * src/game/debugmenu_handler.c, live because the PC build defines
 * LEFTOVERDEBUG) — port-layer memory writes only, no game-code edits.
 * 1 = every solo level selectable at every difficulty plus 007 mode from
 * the first launch. F10 'All unlocked' row toggles it; takes effect next
 * run. Known quirk when ON with a fresh save: audio volumes load as 0
 * (silence) because the patched save block is CRC-valid and skips the
 * game's BLANKSAVEDATA reset that normally seeds max volume — see D259. */
s32 portAllUnlocked = 0;

/* D211: Video.FovScale as a multiplier on the render FOV. Applied game-side
 * at the guPerspectiveF chokepoint (src/fr.c) so it lands BEFORE the CPU
 * pre-multiplies projection x view into the combined world matrix — the
 * fast3d-side matrix hack only caught the handful of pure-perspective loads
 * (pause/watch model, sky) and left the world untouched. 1.0f = original. */
f32 portFovScale = 1.0f;

/* D222: the same widen-FOV math as the fr.c guPerspectiveF chokepoint,
 * factored out so cull-plane / LOD-scale call sites (currentPlayerSetCameraScale,
 * via currentPlayerSetPerspective) can feed the SAME effective FOV that is
 * actually rendered, instead of leaving them on the nominal value. Before this,
 * high FovScale widened what was drawn but left frustum-cull planes and the
 * fog/LOD distance scale (c_scalelod/c_lodscalez) calibrated for the narrower
 * nominal FOV, so on-screen geometry near the edges (and, via c_lodscalez,
 * the fog-based distance-visibility fade) got culled/faded as if the view
 * were still narrow. `isTitleScreen` mirrors fr.c's own
 * `lvlGetCurrentStageToLoad() != LEVELID_TITLE` guard -- the front end's
 * fixed-FOV 3D must not be touched. 1.0f/identity at FovScale=100 (default),
 * bit-for-bit no-op. */
f32 portScaleFovY(f32 fovy, s32 isTitleScreen)
{
    if (!isTitleScreen && portFovScale > 0.4f && portFovScale < 2.01f && portFovScale != 1.0f) {
        fovy *= portFovScale;
        if (fovy > 160.0f) { fovy = 160.0f; }
    }
    return fovy;
}

/* D218: Video.DrawDistance -- multiplier applied to a level's authored
 * Visibility.FarFog (src/game/bgfog.c fogLoadCurrentEnvironment), which is
 * both the far clip plane and the fog-saturation distance (levels are tuned
 * for the stock ~60deg FOV), plus the character/prop fog-visibility-fade
 * cutoff (src/game/propobj.c chrobjFogVisRangeRelated/sub_GAME_7F054C58).
 * Video.DrawDistanceAutoFov (default on) couples the multiplier to
 * Video.FovScale so a wider FOV doesn't clip its own newly visible far
 * geometry -- or fade NPCs out early -- against distances tuned for the
 * narrower original view (D218's "blue artifacting"; the M-121 live
 * playtest found a straight 1:1 FovScale coupling still faded guards in
 * noticeably close on Dam, so the auto coupling is 2x FovScale, not 1x).
 * An explicit Video.DrawDistance != 100 overrides that coupling outright.
 * Clamped to <=4.0x -- pushing the far plane much further out risks
 * far-field z-fighting against the level's original near-plane precision;
 * raised again (M-121 live playtest: 2x FovScale still showed a "blue
 * glow" on far Dam tunnel geometry, so auto coupling is now 4x FovScale)
 * to give headroom (max portFovScale is 1.5 at Video.FovScale's registered
 * ceiling of 150, so 4x tops out at 6.0x -- ceiling raised to match).
 * Identity (1.0f) at both defaults, bit-for-bit no-op. NOTE: end-to-end
 * re-check of fogLoadCurrentEnvironment (bgfog.c) found the fog RAMP
 * itself (not just the far-clip cutoff) already scales correctly with
 * this multiplier -- g_ScaledFarFogIntensity/scaled_far_fog_dist both
 * derive from the same scaled far value. If a visible blue tint at range
 * persists even at a large multiplier, it may be Dam's tunnel sightline
 * simply exceeding whatever distance was tried, or a separate visual
 * element (skybox/backdrop) not gated by Visibility.FarFog at all --
 * worth a fresh screenshot-driven look before assuming another bug here. */
f32 portDrawDistanceMultiplier(void)
{
    f32 mult;
    if (cfgDrawDistance != 100) {
        mult = (f32)cfgDrawDistance / 100.0f;
    } else if (cfgDrawDistanceAutoFov && portFovScale != 1.0f) {
        mult = portFovScale * 4.0f;
    } else {
        return 1.0f;
    }
    if (mult > 6.0f) { mult = 6.0f; }
    if (mult < 1.0f) { mult = 1.0f; }
    return mult;
}

/* D249: Video.LodDistance -- multiplier on the *distance* term
 * modelUpdateDistanceRelations() (src/game/model.c) tests against each LOD
 * node's MinDistance/MaxDistance, composed on top of the game's own
 * g_ModelDistanceScale rather than replacing it. Smaller distance reads as
 * "closer", so this function returns the INVERSE of the requested percent:
 * Video.LodDistance=200 (keep full detail twice as far, more cost) ->
 * 0.5x on the distance term; =50 (drop to lower detail twice as soon, less
 * cost -- the perf lever) -> 2.0x. Video.LodDistanceAutoFov (default OFF,
 * unlike DrawDistanceAutoFov) can couple it to Video.FovScale the same
 * direction as draw distance if ever wanted; off by default so this stays a
 * standalone dial and doesn't quietly add cost as FovScale widens. Clamped
 * to a [0.25, 4.0] distance multiplier (== effective LodDistance 25-400%) --
 * far outside that band either does nothing visible (LOD never triggers) or
 * thrashes every frame. Identity (1.0f) at the Video.LodDistance=100
 * default, bit-for-bit no-op. */
f32 portLodDistanceMultiplier(void)
{
    f32 pct;
    if (cfgLodDistance != 100) {
        pct = (f32)cfgLodDistance;
    } else if (cfgLodDistanceAutoFov && portFovScale != 1.0f) {
        pct = portFovScale * 100.0f;
    } else {
        return 1.0f;
    }
    if (pct < 25.0f)  { pct = 25.0f; }
    if (pct > 400.0f) { pct = 400.0f; }
    return 100.0f / pct;
}

PD_CONSTRUCTOR static void videoConfigInit(void)
{
    configRegisterFloat("Game.ScreenShakeIntensity", &portScreenShakeScale, 0.0f, 10.0f);
#if defined(DAM_ONLY_LAB)
    configRegisterInt("Debug.PerfHUD", &cfgPerfHud, 0, 1);
#endif
    configRegisterInt("Game.SkipIntro", &portSkipIntro, 0, 1);
    configRegisterInt("Game.NoHitFlash", &portNoHitFlash, 0, 1);
    configRegisterInt("Game.AllUnlocked", &portAllUnlocked, 0, 1);
    configRegisterInt("Video.VSync",         &cfgVSync,      0, 1);
    configRegisterInt("Video.FpsCap",        &cfgFpsCap,     0, 1000);
    configRegisterInt("Video.MSAA",          &cfgMSAA,       1, 8);
    configRegisterInt("Video.TextureFilter", &cfgTexFilter,  0, 2);
    configRegisterInt("Video.MipmapFilter",  &cfgMipmapFilter, 0, 3);
    configRegisterInt("Video.FramebufferEffects", &cfgFramebufferEffects, 0, 1);
    configRegisterInt("Video.FixMipTextures", &cfgFixMipTex, 0, 1);
    configRegisterInt("Video.WrapFix", &cfgWrapFix, 0, 1);
    configRegisterInt("Video.FovScale", &cfgFovScale, 50, 150);
    configRegisterInt("Video.DrawDistance", &cfgDrawDistance, 100, 400);
    configRegisterInt("Video.DrawDistanceAutoFov", &cfgDrawDistanceAutoFov, 0, 1);
    configRegisterInt("Video.LodDistance", &cfgLodDistance, 25, 400);
    configRegisterInt("Video.LodDistanceAutoFov", &cfgLodDistanceAutoFov, 0, 1);
    configRegisterInt("Video.Anisotropy", &cfgAniso, 1, 16);
    configRegisterInt("Video.Fullscreen",    &cfgFullscreen, 0, 1);
    configRegisterInt("Window.Width",        &cfgWinW,       0, 16384);
    configRegisterInt("Window.Height",       &cfgWinH,       0, 16384);
    configRegisterInt("Window.X",            &cfgWinX,      -1, 16384);
    configRegisterInt("Window.Y",            &cfgWinY,      -1, 16384);
    configRegisterInt("Window.Maximized",    &cfgWinMax,     0, 1);
}

/* Steam Deck / SteamOS first-run preset. Called from main() when STEAMOS is
 * set, BEFORE configLoad(): if no ge007.ini exists yet, configLoad's
 * first-run path saves these values to disk and every later launch reads the
 * file (user changes via F10 win outright); if an ini already exists its
 * values overwrite everything here. So this is a first-launch preset only.
 * 1280x800 is the Deck's native panel resolution; MSAA 4 + VSync is
 * comfortable headroom for the A11 GPU; DrawDistance/LodDistance at 150%
 * because the authored N64 fade distances read "things pop in just before
 * you can see them" on a sharp 7" close-up panel (e.g. the Dam lock).
 * Note: the panel is 16:10 and the game renders 4:3, so this stretches
 * uniformly like any non-4:3 window today (letterboxing is the parked
 * WIDESCREEN-FOV-PLAN). */
void videoApplySteamOSDefaults(void)
{
    cfgFullscreen   = 1;
    cfgWinW         = 1280;
    cfgWinH         = 800;
    cfgVSync        = 1;
    cfgMSAA         = 4;
    cfgDrawDistance = 150;
    cfgLodDistance  = 150;
}

/* Set by videoRequestLiveConfig() (F10 overlay, host thread); consumed on the
 * scheduler thread in videoStartFrame() where the GL context is bound. */
static volatile int liveCfgDirty = 0;

/* D211/D212: push the port-only image knobs where they apply. FovScale is a
 * plain float the game re-reads each frame; anisotropy goes to fast3d. */
static void videoApplyImageOptions(void)
{
    portFovScale = (f32)cfgFovScale / 100.0f;
    gfx_set_anisotropy_level(cfgAniso);
}

static void videoApplyTexFilter(void)
{
    enum FilteringMode filter;
    if (cfgTexFilter >= 2) filter = FILTER_THREE_POINT;
    else if (cfgTexFilter == 1) filter = FILTER_LINEAR;
    else filter = FILTER_NONE;
    gfx_set_texture_filter(filter);

    /* Auto preserves the behavior this port had before the setting existed:
     * nearest textures use nearest mip selection; bilinear/3-point use linear
     * mip blending. Explicit modes let advanced users override that policy. */
    if (cfgMipmapFilter == 0) {
        gfx_set_mipmap_filter(MIPMAP_DISABLED);
    } else if (cfgMipmapFilter == 1) {
        gfx_set_mipmap_filter(MIPMAP_NEAREST);
    } else if (cfgMipmapFilter == 2) {
        gfx_set_mipmap_filter(MIPMAP_LINEAR);
    } else {
        gfx_set_mipmap_filter(filter == FILTER_NONE ? MIPMAP_NEAREST : MIPMAP_LINEAR);
    }
}

/* Re-apply the live-tunable [Video] knobs (VSync / FpsCap / TextureFilter).
 * MSAA and Fullscreen are FBO/window rebuilds -> "(restart)" in the overlay. */
void videoRequestLiveConfig(void)
{
    liveCfgDirty = 1;
}

/* --- F10 overlay: window / fullscreen changes, deferred to the host thread ---
 * optionsOverlayHandleInput() runs on the scheduler thread; SDL_SetWindowSize /
 * SDL_SetWindowFullscreen pump the Win32 message loop and must run on the
 * window's creating thread. The overlay posts a request here; the host-thread
 * event pump drains it in videoDrainWindowRequests(). */
static volatile int winReqKind = 0;          /* 0 none, 1 resize, 2 fullscreen */
static volatile int winReqA = 0, winReqB = 0;

void videoRequestWindowSize(int w, int h)
{
    winReqA = w; winReqB = h; winReqKind = 1;
}

void videoRequestFullscreen(int on)
{
    winReqA = on ? 1 : 0; winReqKind = 2;
}

void videoGetWindowSize(int *w, int *h)
{
    uint32_t ww = 0, hh = 0; int32_t x = 0, y = 0;
    if (initDone && wmAPI && wmAPI->get_dimensions) {
        wmAPI->get_dimensions(&ww, &hh, &x, &y);
    }
    if (w) *w = (int)ww;
    if (h) *h = (int)hh;
}

void videoGetDesktopSize(int *w, int *h)
{
    SDL_DisplayMode m;
    memset(&m, 0, sizeof(m));
    if (SDL_GetDesktopDisplayMode(0, &m) != 0 || m.w <= 0 || m.h <= 0) {
        m.w = 1920; m.h = 1080;
    }
    if (w) *w = m.w;
    if (h) *h = m.h;
}

int videoIsFullscreen(void)
{
    return (initDone && wmAPI && wmAPI->get_fullscreen_state)
         ? (wmAPI->get_fullscreen_state() ? 1 : 0) : 0;
}

static void videoDrainWindowRequests(void)
{
    int kind = winReqKind;
    if (!kind || !wmAPI) {
        winReqKind = 0;
        return;
    }
    winReqKind = 0;

    if (kind == 1) {
        int w = winReqA, h = winReqB;
        int32_t px = 100, py = 100;
        if (wmAPI->get_fullscreen_state && wmAPI->get_fullscreen_state()) {
            if (wmAPI->set_fullscreen) wmAPI->set_fullscreen(false);
            cfgFullscreen = 0;
        }
        if (wmAPI->get_centered_positions) {
            wmAPI->get_centered_positions(w, h, &px, &py);
        }
        if (wmAPI->set_dimensions) {
            wmAPI->set_dimensions((uint32_t)w, (uint32_t)h, px, py);
        }
        gfx_sdl_update_cached_size();
        cfgWinW = w; cfgWinH = h;
        sysLogPrintf(LOG_INFO, "video: window -> %dx%d", w, h);
    } else if (kind == 2) {
        int on = winReqA;
        if (wmAPI->set_fullscreen) wmAPI->set_fullscreen(on != 0);
        gfx_sdl_update_cached_size();
        cfgFullscreen = on ? 1 : 0;
        sysLogPrintf(LOG_INFO, "video: fullscreen %s", on ? "on" : "off");
    }
}

static u32 frames = 0;
/* Set by the host event pump (F12), consumed on the render thread in
 * videoEndFrame where a GL context is current. */
static volatile int screenshotReq = 0;

/* Pre-swap capture hook (defined below, registered in videoInit). */
static void videoPreSwapCapture(void);
extern void (*gfx_pre_swap_hook)(void);
static double fpsWindowStart = 0.0;
static int fpsNumFrames = 0;
static float vidAvgFPS = 0.f;

int videoInit(void)
{
    wmAPI = &gfx_sdl;
    renderingAPI = &gfx_opengl_api;

    gfx_current_native_viewport.width = GE_NATIVE_W;
    gfx_current_native_viewport.height = GE_NATIVE_H;
    gfx_current_native_aspect = (float)GE_NATIVE_W / (float)GE_NATIVE_H;
    gfx_framebuffers_enabled = cfgFramebufferEffects != 0;
    /* GoldenEye has no true detail textures; enabling this fast3d path selects
     * the wrong mip tile for GE room GDLs (D107), so it is intentionally not
     * user-exposed. */
    gfx_detail_textures_enabled = false;

    /* MSAA: snap the requested sample count down to a supported power of two. */
#if defined(__aarch64__) && defined(USE_GLES)
    /* R36S target policy: use the direct window framebuffer. The handheld
     * baseline does not depend on multisample FBO/resolve support, which varies
     * across its Mesa/EGL stacks; expose one deterministic GLES3 path. */
    if (cfgMSAA != 1) {
        sysLogPrintf(LOG_NOTE, "R36S video diagnostic: forcing MSAA 1 (configured %d)", cfgMSAA);
    }
    gfx_msaa_level = 1;
#else
    gfx_msaa_level = cfgMSAA >= 8 ? 8 : cfgMSAA >= 4 ? 4 : cfgMSAA >= 2 ? 2 : 1;
#endif

    int winW = cfgWinW > 0 ? cfgWinW : 0;   /* 0 -> gfx_sdl2 auto-fits to the desktop */
    int winH = cfgWinH > 0 ? cfgWinH : 0;
    int havePos = (cfgWinX >= 0 && cfgWinY >= 0);

    struct GfxInitSettings set = {
        .wapi = wmAPI,
        .rapi = renderingAPI,
        .window_settings = {
            .title = "GoldenEye 007",
            .width = winW,
            .height = winH,
            .x = havePos ? cfgWinX : 100,
            .y = havePos ? cfgWinY : 100,
            .fullscreen = cfgFullscreen != 0,
            .fullscreen_is_exclusive = false,
            .maximized = cfgWinMax != 0,
            .centered = !havePos,
            .allow_hidpi = false,
        },
    };

    gfx_init(&set);
    gfx_sdl_update_cached_size();

#if defined(__aarch64__)
    {
        int sw = 0, sh = 0;
        int dw = 0, dh = 0;
        GLint vp[4] = {0, 0, 0, 0};
        GLint fbo = -1;
        SDL_Window *window = wmAPI && wmAPI->get_window_handle
                           ? (SDL_Window *)wmAPI->get_window_handle() : NULL;
        if (window) {
            SDL_GetWindowSize(window, &sw, &sh);
            SDL_GL_GetDrawableSize(window, &dw, &dh);
        }
        glGetIntegerv(GL_VIEWPORT, vp);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        sysLogPrintf(LOG_NOTE,
            "R36S display diagnostic: SDL window=%dx%d drawable=%dx%d "
            "gfx=%ux%u viewport=%d,%d %dx%d fbo=%d",
            sw, sh, dw, dh,
            gfx_current_dimensions.width, gfx_current_dimensions.height,
            vp[0], vp[1], vp[2], vp[3], (int)fbo);
    }
#endif

    /* VSync + optional fps cap; fast3d paces the window itself. */
    wmAPI->set_swap_interval(cfgVSync ? 1 : 0);
    /* D186: a low cap does not just drop frames -- the pacing wait blocks the
     * scheduler thread and throttles the sim with it. Normalise a bad
     * ge007.ini value (e.g. dinged to 10 via the options overlay) to uncapped
     * so it persists sane on the next configSave(). */
    if (cfgFpsCap > 0 && cfgFpsCap < 30) {
        sysLogPrintf(LOG_WARNING, "video: Video.FpsCap=%d too low (throttles the sim); using 0 (uncapped)", cfgFpsCap);
        cfgFpsCap = 0;
    }
    gfx_set_target_fps(cfgFpsCap);   /* 0 = uncapped */

    /* Texture filtering. 1 = bilinear (default, matches prior behaviour),
     * 0 = crisp nearest, 2 = N64 3-point emulation + trilinear mips (opt-in;
     * more console-authentic but softens textures at normal distance -- did
     * NOT fix the Depot roof, see docs/BRIEF-B2-depot-textures.md). All keep
     * point-sampled tiles (HUD, G_TF_POINT) crisp via the per-tile flag. */
    gfx_set_fix_mip_textures(cfgFixMipTex);
    gfx_set_wrap_fix(cfgWrapFix);

    videoApplyTexFilter();
    videoApplyImageOptions();

    /* The GL context is currently current on this (host main) thread, but all
     * rendering happens on the game's scheduler thread. WGL only allows a
     * context to be current on one thread at a time, so release it here; the
     * scheduler thread re-binds it per frame via gfx_sdl_make_context_current()
     * (see videoStartFrame). Must come after set_swap_interval above, which
     * still needs a current context on this thread. */
    gfx_sdl_release_context();

    gfx_pre_swap_hook = videoPreSwapCapture;

    initDone = 1;
#if defined(DAM_ONLY_LAB)
    damLabInit();
#endif
    sysLogPrintf(LOG_INFO, "video: %dx%d window (native %dx%d)",
                 (int)gfx_current_dimensions.width, (int)gfx_current_dimensions.height,
                 GE_NATIVE_W, GE_NATIVE_H);
    return 0;
}

void videoDestroy(void)
{
    if (initDone) {
#if defined(DAM_ONLY_LAB)
        damLabShutdown();
#endif
        /* GL resources must be deleted before the SDL context/window. */
        gfx_sdl_make_context_current();
        gfx_pre_swap_hook = NULL;
        gfx_destroy();
        initDone = 0;
    }
}

void videoStartFrame(void)
{
    if (!initDone) {
        return;
    }
    /* Rendering runs on the game's scheduler thread; the GL context was
     * created on the host main thread. */
    gfx_sdl_make_context_current();

    if (liveCfgDirty) {
        liveCfgDirty = 0;
        wmAPI->set_swap_interval(cfgVSync ? 1 : 0);
        gfx_set_target_fps(cfgFpsCap);   /* 0 = uncapped */
        videoApplyTexFilter();
        videoApplyImageOptions();
        sysLogPrintf(LOG_INFO, "video: live config applied "
                     "(vsync=%d fpscap=%d texfilter=%d fov=%d aniso=%d)",
                     cfgVSync, cfgFpsCap, cfgTexFilter, cfgFovScale, cfgAniso);
    }

    gfx_start_frame();
}

/*
 * Host-thread SDL event pump.
 *
 * On Windows, window messages are only dispatched when the thread that
 * CREATED the window pumps them — and every game thread can be blocked on a
 * message queue at any time. So the host main thread (which created the
 * window in videoInit) must keep pumping; otherwise the window goes
 * "Not Responding" and ESC/close never arrive. fast3d's own handle_events
 * (which runs during rendering) remains as a backstop.
 */

#if defined(__aarch64__)
static const unsigned char *crashGlyph(char c)
{
    /* 5x7 uppercase font; each row uses bits 4..0. Only characters used by
     * the crash overlay are included. */
    static const unsigned char blank[7] = {0,0,0,0,0,0,0};
    static const unsigned char g0[7] = {14,17,19,21,25,17,14};
    static const unsigned char g1[7] = {4,12,4,4,4,4,14};
    static const unsigned char g2[7] = {14,17,1,2,4,8,31};
    static const unsigned char g3[7] = {30,1,1,14,1,1,30};
    static const unsigned char g4[7] = {2,6,10,18,31,2,2};
    static const unsigned char g5[7] = {31,16,16,30,1,1,30};
    static const unsigned char g6[7] = {14,16,16,30,17,17,14};
    static const unsigned char g7[7] = {31,1,2,4,8,8,8};
    static const unsigned char g8[7] = {14,17,17,14,17,17,14};
    static const unsigned char g9[7] = {14,17,17,15,1,1,14};
    static const unsigned char A[7] = {14,17,17,31,17,17,17};
    static const unsigned char B[7] = {30,17,17,30,17,17,30};
    static const unsigned char C[7] = {15,16,16,16,16,16,15};
    static const unsigned char D[7] = {30,17,17,17,17,17,30};
    static const unsigned char E[7] = {31,16,16,30,16,16,31};
    static const unsigned char F[7] = {31,16,16,30,16,16,16};
    static const unsigned char G[7] = {15,16,16,23,17,17,15};
    static const unsigned char H[7] = {17,17,17,31,17,17,17};
    static const unsigned char I[7] = {14,4,4,4,4,4,14};
    static const unsigned char L[7] = {16,16,16,16,16,16,31};
    static const unsigned char M[7] = {17,27,21,21,17,17,17};
    static const unsigned char N[7] = {17,25,21,19,17,17,17};
    static const unsigned char O[7] = {14,17,17,17,17,17,14};
    static const unsigned char P[7] = {30,17,17,30,16,16,16};
    static const unsigned char R[7] = {30,17,17,30,20,18,17};
    static const unsigned char S[7] = {15,16,16,14,1,1,30};
    static const unsigned char T[7] = {31,4,4,4,4,4,4};
    static const unsigned char U[7] = {17,17,17,17,17,17,14};
    static const unsigned char X[7] = {17,17,10,4,10,17,17};
    static const unsigned char Y[7] = {17,17,10,4,4,4,4};
    static const unsigned char dash[7] = {0,0,0,31,0,0,0};
    static const unsigned char colon[7] = {0,4,4,0,4,4,0};

    switch (c) {
    case '0': return g0; case '1': return g1; case '2': return g2;
    case '3': return g3; case '4': return g4; case '5': return g5;
    case '6': return g6; case '7': return g7; case '8': return g8;
    case '9': return g9; case 'A': return A; case 'B': return B;
    case 'C': return C; case 'D': return D; case 'E': return E; case 'F': return F;
    case 'G': return G; case 'H': return H; case 'I': return I;
    case 'L': return L; case 'M': return M; case 'N': return N; case 'O': return O;
    case 'P': return P; case 'R': return R; case 'S': return S;
    case 'T': return T; case 'U': return U; case 'X': return X; case 'Y': return Y;
    case '-': return dash; case ':': return colon;
    default: return blank;
    }
}

static void crashDrawRect(int x, int y, int w, int h, int screenH)
{
    glScissor(x, screenH - y - h, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void crashDrawText(int x, int y, int scale, int screenH, const char *text)
{
    int cx = x;
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    for (; *text; ++text) {
        if (*text == '\n') {
            y += 9 * scale;
            cx = x;
            continue;
        }
        const unsigned char *rows = crashGlyph(*text);
        for (int ry = 0; ry < 7; ++ry) {
            for (int rx = 0; rx < 5; ++rx) {
                if (rows[ry] & (1u << (4 - rx))) {
                    crashDrawRect(cx + rx * scale, y + ry * scale,
                                  scale, scale, screenH);
                }
            }
        }
        cx += 6 * scale;
    }
}

static void crashHex(char *dst, uintptr_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = (int)(sizeof(uintptr_t) * 2);
    dst[0] = '0'; dst[1] = 'X';
    for (int i = 0; i < n; ++i) {
        dst[2 + i] = hex[(v >> ((n - 1 - i) * 4)) & 0xF];
    }
    dst[2 + n] = 0;
}

static void videoDrawCrashScreen(const CrashScreenInfo *ci)
{
    int w = 640, h = 480;
    SDL_Window *window = wmAPI && wmAPI->get_window_handle
                       ? (SDL_Window *)wmAPI->get_window_handle() : NULL;
    if (window) {
        SDL_GL_GetDrawableSize(window, &w, &h);
        if (w <= 0) w = 640;
        if (h <= 0) h = 480;
    }

    gfx_sdl_make_context_current();
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);

    glScissor(0, 0, w, h);
    glClearColor(0.22f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    char pc[2 + sizeof(uintptr_t) * 2 + 1];
    char lr[2 + sizeof(uintptr_t) * 2 + 1];
    char sp[2 + sizeof(uintptr_t) * 2 + 1];
    char fault[2 + sizeof(uintptr_t) * 2 + 1];
    char sig[4] = {'0','0',0,0};
    crashHex(pc, ci->pc);
    crashHex(lr, ci->lr);
    crashHex(sp, ci->sp);
    crashHex(fault, ci->fault);
    sig[0] = (char)('0' + ((ci->signal / 10) % 10));
    sig[1] = (char)('0' + (ci->signal % 10));

    const int scale = (w >= 600 && h >= 400) ? 3 : 2;
    int y = 24;
    crashDrawText(24, y, scale, h, "GE007 CRASH"); y += 12 * scale;
    crashDrawText(24, y, scale, h, "SIGNAL "); crashDrawText(24 + 7*6*scale, y, scale, h, sig); y += 10 * scale;
    crashDrawText(24, y, scale, h, "PC "); crashDrawText(24 + 3*6*scale, y, scale, h, pc); y += 10 * scale;
    crashDrawText(24, y, scale, h, "LR "); crashDrawText(24 + 3*6*scale, y, scale, h, lr); y += 10 * scale;
    crashDrawText(24, y, scale, h, "SP "); crashDrawText(24 + 3*6*scale, y, scale, h, sp); y += 10 * scale;
    crashDrawText(24, y, scale, h, "FAULT "); crashDrawText(24 + 6*6*scale, y, scale, h, fault); y += 14 * scale;
    crashDrawText(24, y, scale, h, "PRESS BUTTON TO EXIT");

    glDisable(GL_SCISSOR_TEST);
    if (wmAPI && wmAPI->swap_buffers_begin) wmAPI->swap_buffers_begin();
    if (wmAPI && wmAPI->swap_buffers_end) wmAPI->swap_buffers_end();
    crashScreenActive = 1;
    crashScreenShown();
}
#endif

void videoPumpEvents(void)
{
    if (!initDone) {
        return;
    }

#if defined(__aarch64__)
    const CrashScreenInfo *ci = crashGetScreenInfo();
    if (ci && ci->pending && !crashScreenActive) {
        videoDrawCrashScreen(ci);
    }
#endif

    /* Apply any window/fullscreen change the F10 overlay posted from the
     * scheduler thread (must run here, on the window's creating thread). */
    if (!crashScreenActive) {
        videoDrainWindowRequests();
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
#if defined(__aarch64__)
        if (crashScreenActive) {
            if (ev.type == SDL_QUIT || ev.type == SDL_KEYDOWN ||
                ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_JOYBUTTONDOWN) {
                crashScreenDismiss();
            }
            continue;
        }
#endif
        switch (ev.type) {
        case SDL_QUIT:
            sysLogPrintf(LOG_INFO, "video: quit requested");
            exit(0);
            break;
        case SDL_KEYDOWN:
            /* D145: bare ESC used to exit(0). On the front-end / debrief
             * screens ESC is the natural "back" key, so a player pressing it
             * to page back instead quit the whole game (looked like a crash --
             * clean exit, no crash log). ESC now feeds the N64 B button
             * (back / cancel) via input.c; quitting is window-close (the X) or
             * Alt+F4 only. */
            if ((ev.key.keysym.sym == SDLK_F4) && (ev.key.keysym.mod & KMOD_ALT)) {
                sysLogPrintf(LOG_INFO, "video: Alt+F4 -> quit");
                exit(0);
            } else if ((ev.key.keysym.sym == SDLK_RETURN) && (ev.key.keysym.mod & KMOD_ALT) && !ev.key.repeat) {
                if (wmAPI && wmAPI->set_fullscreen && wmAPI->get_fullscreen_state) {
                    wmAPI->set_fullscreen(!wmAPI->get_fullscreen_state());
                    gfx_sdl_update_cached_size();
                }
            } else if (ev.key.keysym.sym == SDLK_F12 && !ev.key.repeat) {
                screenshotReq = 1;
            } else if (ev.key.keysym.sym == SDLK_F10 && !ev.key.repeat) {
                optionsOverlayToggle();   /* F10: port-layer options overlay */
            } else if (ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat) {
                /* Overlay open: ESC closes it (and is swallowed). Otherwise
                 * WI-1: in click-to-lock mode ESC frees the captured cursor
                 * (and is swallowed); else it falls through to input.c where
                 * it feeds the N64 B button (D145). */
                if (optionsOverlayIsOpen()) {
                    optionsOverlayToggle();
                } else {
                    inputReleaseCapture();
                }
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            /* WI-1: a click in the window (re)locks the cursor in
             * click-to-lock mode; a no-op otherwise. */
            if (!optionsOverlayIsOpen()) {
                inputNotifyClick();
            }
            break;
        case SDL_MOUSEWHEEL:
            if (optionsOverlayIsOpen()) {
                optionsOverlayScroll(ev.wheel.y);   /* move the selection */
            } else {
                inputPostWheel(ev.wheel.y);   /* weapon cycle */
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            inputRescanPads();
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                sysLogPrintf(LOG_INFO, "video: window closed");
                exit(0);
            } else if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                gfx_sdl_update_cached_size();
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                inputSetMouseGrab(0);   /* free the cursor when alt-tabbed away */
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                inputSetMouseGrab(1);
            }
            break;
        default:
            break;
        }
    }

    /* Refresh the window title with the live FPS about once a second. */
    if (wmAPI && wmAPI->set_window_title) {
        static double lastTitle = 0.0;
        double now = wmAPI->get_time();
        if (now - lastTitle >= 1.0) {
            lastTitle = now;
            char title[64];
            snprintf(title, sizeof(title), "GoldenEye 007  -  %.0f fps", vidAvgFPS);
            wmAPI->set_window_title(title);
        }
    }
}

void videoSubmitCommands(Gfx *cmds)
{
    if (!initDone) {
        return;
    }
    gfx_run(cmds);
}

/* Runs from gfx_sdl_swap_buffers_begin with the composited frame still in the
 * back buffer, just before SDL_GL_SwapWindow. Reading the back buffer after
 * the swap is undefined on buffer-exchange drivers (Mesa/WSLg) -> black. */
static void videoPreSwapCapture(void)
{
#if defined(__aarch64__)
    /* R36S presentation instrumentation is opt-in. It deliberately modifies
     * framebuffer 0 and performs synchronous readback, so it must never run
     * during normal gameplay. Set GE_VIDEO_DIAG=1 for bring-up diagnostics. */
    static int videoDiag = -1;
    if (videoDiag < 0) {
        const char *env = getenv("GE_VIDEO_DIAG");
        videoDiag = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    if (videoDiag) {
    /* R36S physical-presentation proof. Draw directly into framebuffer 0
     * immediately before SDL_GL_SwapWindow, using only scissored clears.
     * This bypasses GoldenEye display lists, shaders, textures and fonts. */
    {
        GLint prevFbo = 0;
        GLint prevVp[4] = {0, 0, 640, 480};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VIEWPORT, prevVp);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, (GLsizei)gfx_current_dimensions.width,
                         (GLsizei)gfx_current_dimensions.height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glEnable(GL_SCISSOR_TEST);

        /* Red title bar + white bitmap text: unmistakable but still leaves
         * most of the GoldenEye frame visible behind it. */
        glClearColor(0.70f, 0.0f, 0.0f, 1.0f);
        crashDrawRect(0, 0, (int)gfx_current_dimensions.width, 42,
                      (int)gfx_current_dimensions.height);
        crashDrawText(12, 8, 3, (int)gfx_current_dimensions.height,
                      "GE007 VIDEO DEBUG");

        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    }

    /* R36S black-screen diagnostic: sample five 32x32 regions of the actual
     * composited back buffer immediately before SDL_GL_SwapWindow. If these
     * contain colour while the LCD stays black, rendering is fine and the
     * remaining bug is presentation/scanout. */
    if (frames < 10 || (frames % 300) == 0) {
        enum { SW = 32, SH = 32 };
        unsigned char px[SW * SH * 4];
        const int w = (int)gfx_current_dimensions.width;
        const int h = (int)gfx_current_dimensions.height;
        const int xs[5] = { 0, w - SW, 0, w - SW, (w - SW) / 2 };
        const int ys[5] = { 0, 0, h - SH, h - SH, (h - SH) / 2 };
        unsigned total_nonblack = 0;
        unsigned maxrgb = 0;
        GLint fbo = -1;
        GLint vp[4] = {0, 0, 0, 0};
        GLint readFmt = 0, readType = 0;
        GLenum stale = GL_NO_ERROR;
        while ((stale = glGetError()) != GL_NO_ERROR) { }
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, vp);
        glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &readFmt);
        glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &readType);
        for (int r = 0; r < 5; ++r) {
            int x = xs[r] < 0 ? 0 : xs[r];
            int y = ys[r] < 0 ? 0 : ys[r];
            memset(px, 0, sizeof(px));
            glReadPixels(x, y, SW, SH, GL_RGBA, GL_UNSIGNED_BYTE, px);
            for (int i = 0; i < SW * SH; ++i) {
                unsigned rr = px[i * 4 + 0];
                unsigned gg = px[i * 4 + 1];
                unsigned bb = px[i * 4 + 2];
                unsigned m = rr > gg ? rr : gg;
                if (bb > m) m = bb;
                if (m) ++total_nonblack;
                if (m > maxrgb) maxrgb = m;
            }
        }
        sysLogPrintf(LOG_NOTE,
            "R36S preswap frame=%u fbo=%d viewport=%d,%d %dx%d sampled_nonblack=%u/5120 maxrgb=%u readfmt=0x%X readtype=0x%X glerr=0x%X",
            frames, (int)fbo, vp[0], vp[1], vp[2], vp[3],
            total_nonblack, maxrgb, (unsigned)readFmt, (unsigned)readType,
            (unsigned)glGetError());
    }
    }
#endif

#if defined(DAM_ONLY_LAB) && defined(__aarch64__)
    /* DAMLAB HUD: deliberately simple framebuffer text so the diagnostic
     * remains visible even if the game UI/render state is part of the bug.
     * Debug.PerfHUD only controls visibility; telemetry/logging stays live. */
    if (cfgPerfHud) {
        GLint prevFbo = 0;
        GLint prevVp[4] = {0, 0, 640, 480};
        char hud[256];
        const int w = (int)gfx_current_dimensions.width;
        const int h = (int)gfx_current_dimensions.height;

        damLabFormatOverlay(hud, sizeof(hud));
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VIEWPORT, prevVp);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glEnable(GL_SCISSOR_TEST);
        glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
        crashDrawRect(4, 4, 300, 108, h);
        crashDrawText(10, 10, 2, h, hud);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    }
#endif

    /* GE_PCDUMP="first-last" / "first-last:step" -> ./ppm/frame_NNNNNN.ppm.
     * Also honours [Debug] FrameDump in ge007.ini (env var wins). */
    const char *pcdump = configGetFrameDump();
    if (pcdump) {
        static int lo = -1, hi = 0, step = 1;
        if (lo < 0) {
            const char *v = pcdump;
            lo = 1; hi = 0x7fffffff; step = 1;
            sscanf(v, "%d-%d:%d", &lo, &hi, &step);
            if (sscanf(v, "%d-%d", &lo, &hi) != 2)
                hi = 0x7fffffff;
            GE_MKDIR("ppm");
        }
        if ((int)frames >= lo && (int)frames <= hi &&
            ((int)frames - lo) % step == 0) {
            char path[128];
            snprintf(path, sizeof(path), "ppm/frame_%06d.ppm", (int)frames);
            gfx_opengl_dump_bound_fbo((uint32_t)gfx_current_dimensions.width,
                                      (uint32_t)gfx_current_dimensions.height, path);
        }
    }

    if (screenshotReq) {
        screenshotReq = 0;
        static int shotNum = 0;
        char path[128];
        GE_MKDIR("ppm");
        snprintf(path, sizeof(path), "ppm/shot_%03d.ppm", shotNum++);
        if (gfx_opengl_dump_bound_fbo((uint32_t)gfx_current_dimensions.width,
                                      (uint32_t)gfx_current_dimensions.height, path)) {
            sysLogPrintf(LOG_INFO, "video: screenshot -> %s "
                         "(view with tools_pc/ppm2bmp.py)", path);
        } else {
            sysLogPrintf(LOG_WARNING, "video: screenshot failed");
        }
    }
}

void videoEndFrame(void)
{
    if (!initDone) {
        return;
    }
    gfx_end_frame();

    ++frames;
    ++fpsNumFrames;

    double now = wmAPI->get_time();
    if (fpsWindowStart == 0.0) {
        fpsWindowStart = now;
    }
    if (now - fpsWindowStart >= 1.0) {
        vidAvgFPS = (float)(fpsNumFrames / (now - fpsWindowStart));
        fpsNumFrames = 0;
        fpsWindowStart = now;
#if defined(DAM_ONLY_LAB)
        damLabHostSample(vidAvgFPS);
#endif
    }
}

float videoGetFPS(void)
{
    return vidAvgFPS;
}

/*
 * Snapshot the current window geometry into the [Window] / [Video] config
 * vars so the next configSave() persists it. Called from main.c's atexit
 * handler (runs on the host thread, which owns the window). A maximized or
 * fullscreen window keeps its last restored size/pos on disk; only the
 * flag is updated.
 */
void videoSaveWindowState(void)
{
    if (!initDone || !wmAPI) {
        return;
    }

    int32_t fs = wmAPI->get_fullscreen_state ? wmAPI->get_fullscreen_state() : 0;
    int32_t mx = wmAPI->get_maximized_state ? wmAPI->get_maximized_state() : 0;
    cfgFullscreen = fs ? 1 : 0;
    cfgWinMax = mx ? 1 : 0;

    if (!fs && !mx && wmAPI->get_dimensions) {
        uint32_t w = 0, h = 0;
        int32_t x = 0, y = 0;
        wmAPI->get_dimensions(&w, &h, &x, &y);
        if (w > 0 && h > 0) {
            cfgWinW = (int)w;
            cfgWinH = (int)h;
            cfgWinX = x < 0 ? 0 : x;
            cfgWinY = y < 0 ? 0 : y;
        }
    }
}

void videoUpdateNativeResolution(s32 w, s32 h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_current_native_viewport.width = w;
    gfx_current_native_viewport.height = h;
    gfx_current_native_aspect = (float)w / (float)h;
}

s32 videoGetNativeWidth(void)  { return gfx_current_native_viewport.width; }
s32 videoGetNativeHeight(void) { return gfx_current_native_viewport.height; }

s32 videoCreateFramebuffer(u32 w, u32 h, s32 upscale, s32 autoresize)
{
    return gfx_create_framebuffer(w, h, upscale, autoresize);
}

void videoCopyFramebuffer(s32 dst, s32 src, s32 left, s32 top)
{
    /* assume immediate copies always read the front buffer */
    gfx_copy_framebuffer(dst, src, left, top, false);
}

void videoResetTextureCache(void)
{
    gfx_texture_cache_clear();
}

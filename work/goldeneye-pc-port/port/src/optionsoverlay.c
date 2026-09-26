/*
 * ARM-GE Port Control overlay.
 *
 * F10 retains the historical binding, but the old flat PC-options list has
 * been replaced by a page-based controller/mouse/keyboard UI.
 *
 * Port-layer only. No src/ menu code is touched: the overlay draws its own
 * fast3d 2D display list (appended after the game DL in gfx_run) and edits the
 * port-owned config.c variables directly. Live knobs apply immediately; the
 * two that need an FBO/window rebuild (MSAA, Fullscreen) are tagged "(restart)".
 *
 * The panel adapts to whatever 2D space it is drawn in (320x240 in-game vs
 * 440x330 on front-end screens -- viSetXY differs) and scrolls when the row
 * list outgrows the viewport (wheel / arrows at the edges). Cyclic rows
 * (MSAA, texture filter, resolution, toggles) wrap in both directions; the
 * manual % rows (draw/LOD distance) are hidden while their "auto" toggle is on.
 *
 * Text + fill helpers are the game's own (textRender / microcode_constructor /
 * gDPFillRectangle) reached by extern -- same pattern input.c uses to read
 * current_menu / cursor_h_pos. This is a rendering/UI view, not a logic change.
 *
 * Diagnostic: set GE_OPTIONSOVERLAY=1 to auto-open at boot (headless layout
 * check). Env-gated, harmless when unset.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <PR/ultratypes.h>
/* gbi.h's gDP* DL macros use _SHIFTL/_SHIFTR but do not define them -- game TUs
 * get them from <ultra64.h>/<PR/mbi.h>, which also drags in N64 OS headers that
 * shadow libc here. Define the two pure macros locally (verbatim from mbi.h) so
 * this stays a plain port TU. Without them GCC/ld fails "undefined reference to
 * _SHIFTL" (MinGW's chain happens to provide it). */
#ifndef _SHIFTL
#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w) ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))
#endif
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "optionsoverlay.h"
#include "damlab.h"
#include "systemperf.h"

/* ---- game symbols (rendering/UI only; see input.c for the same pattern) ---- */
struct font;
struct fontchar;
extern struct font     *ptrFontBankGothic;
extern struct fontchar *ptrFontBankGothicChars;
extern Gfx  *microcode_constructor(Gfx *gdl);
extern Gfx  *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars,
                        struct font *font, u32 colour, s32 width, s32 height,
                        u32 yOffset, s32 lineheight);
extern void  textMeasure(s32 *textheight, s32 *textwidth, char *text,
                         struct fontchar *chars, struct font *font, s32 lineheight);
extern s16   viGetX(void);
extern s16   viGetY(void);

/* GoldenEye-owned display settings. These are the same getters/setters used
 * by the in-watch options page; Port Control is only another UI surface. */
extern u32 get_screen_ratio(void);
extern void set_screen_ratio(u32 ratio);
extern u32 cur_player_get_screen_setting(void);
extern void cur_player_set_screen_setting(u32 value);
extern u32 get_cur_player_look_vertical_inverted(void);
extern void set_cur_player_look_vertical_inverted(u32 value);
extern s32 cur_player_get_autoaim(void);
extern void cur_player_set_autoaim(u32 value);
extern u32 cur_player_get_aim_control(void);
extern void cur_player_set_aim_control(u32 value);
extern u32 cur_player_get_sight_onscreen_control(void);
extern void cur_player_set_sight_onscreen_control(u32 value);
extern u32 cur_player_get_lookahead(void);
extern void cur_player_set_lookahead(u32 value);
extern u32 cur_player_get_ammo_onscreen_setting(void);
extern void cur_player_set_ammo_onscreen_setting(u32 value);
extern u16 get_mTrack2Vol(void);
extern void set_mTrack2Vol(u16 value);
extern u16 call_sndGetSfxSlotFirstNaturalVolume(void);
extern void sub_GAME_7F0A91A0(u16 value);

/* Narrow ABI bridge for the original cheat handlers. Do not include the
 * full game constants/type graph in this port-layer UI file: the CHEAT_ID
 * enum is an int ABI and these verified values are stable in bondconstants.h. */
enum {
    GE_CHEAT_UNUSED = 0,
    GE_CHEAT_INVINCIBILITY = 2,
    GE_CHEAT_ALLGUNS = 3,
    GE_CHEAT_MAXAMMO = 4,
    GE_CHEAT_LINEMODE = 7,
    GE_CHEAT_INVISIBILITY = 10,
    GE_CHEAT_INFINITE_AMMO = 11,
    GE_CHEAT_DK_MODE = 12,
    GE_CHEAT_EXTRA_WEAPONS = 13,
    GE_CHEAT_TINY_BOND = 14,
    GE_CHEAT_PAINTBALL = 15,
    GE_CHEAT_10X_HEALTH = 16,
    GE_CHEAT_MAGNUM = 17,
    GE_CHEAT_LASER = 18,
    GE_CHEAT_GOLDEN_GUN = 19,
    GE_CHEAT_TURBO_MODE = 24,
    GE_CHEAT_ENEMY_ROCKETS = 28,
    GE_CHEAT_2X_ROCKET_LAUNCHER = 29,
    GE_CHEAT_2X_GRENADE_LAUNCHER = 30,
    GE_CHEAT_2X_RCP90 = 31
};

/* GoldenEye's original cheat machinery. Port Control invokes the same
 * handlers as the cartridge button-code path; no parallel cheat state. */
extern bool cheatIsActive(int cheat);
extern void cheatButtonTurnOnCheatForPlayers(int cheat);
extern void cheatButtonHandleCheatsTurnedOff(int cheat);
extern void cheatDisableAllCheats(void);

/* ------------------------------------------------------------------------ */

enum { ROW_TOGGLE, ROW_SLIDER, ROW_ENUM, ROW_MSAA, ROW_RES, ROW_ACTION, ROW_BIND };
enum { PAGE_GRAPHICS, PAGE_AUDIO, PAGE_INPUT, PAGE_GAMEPLAY, PAGE_CHEATS, PAGE_SYSTEM, PAGE_COUNT };

static const char *const kPageNames[PAGE_COUNT] = {
    "VIDEO", "AUDIO", "INPUT", "GAME", "CHEATS", "SYSTEM"
};

static const char *const kPageHints[PAGE_COUNT] = {
    "DISPLAY / RENDER / FOV",
    "MIXER / LATENCY / OUTPUT",
    "MOUSE / PAD / AIM",
    "GAMEPLAY / ACCESS",
    "ORIGINAL GOLDENEYE FUN",
    "TELEMETRY / GOVERNORS / MEMORY"
};

static const char *const kOnOff[]     = { "OFF", "ON", NULL };
static const char *const kTexFilter[]    = { "NEAREST", "BILINEAR", "3-POINT", NULL };
static const char *const kMipmapFilter[] = { "OFF", "NEAREST", "TRILINEAR", "AUTO", NULL };
static const char *const kScreenMode[]   = { "FULL", "WIDE", "CINEMA", NULL };
static const char *const kScreenRatio[]  = { "NORMAL", "16:9", NULL };
static const char *const kAimControl[]    = { "HOLD", "TOGGLE", NULL };
static const char *const kGraphicsPreset[]= { "CUSTOM", "N64", "CRISP", "ENHANCED", "R36S", NULL };
static const char *const kAudioPreset[]   = { "CUSTOM", "LOW LATENCY", "BALANCED", "SAFE", NULL };
static const char *const kTaaMode[]       = { "OFF", "LOW", "HIGH", NULL };
static const char *const kCpuGovernor[]   = { "SYSTEM", "SCHEDUTIL", "PERFORMANCE", "POWERSAVE", NULL };
static const char *const kGpuGovernor[]   = { "SYSTEM", "SIMPLE ONDEMAND", "PERFORMANCE", "POWERSAVE", NULL };
static const char *const kRamProfile[]    = { "SYSTEM", "LOW SWAP", "BALANCED", NULL };
static const int         kMsaaSeq[]   = { 1, 2, 4, 8 };

/* Windowed-mode resolution presets. Filtered at init to those that fit the
 * desktop; the Resolution row cycles the surviving list. */
static const int kResList[][2] = {
    {  640,  480 }, {  800,  600 }, {  960,  720 }, { 1024,  768 },
    { 1152,  864 }, { 1280,  720 }, { 1280,  800 }, { 1280,  960 },
    { 1366,  768 }, { 1440,  900 }, { 1600,  900 }, { 1600, 1200 },
    { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 },
    { 3200, 1800 }, { 3840, 2160 },
};
#define NUM_RES ((int)(sizeof(kResList) / sizeof(kResList[0])))
static int s_resFit[NUM_RES];   /* indices into kResList that fit the desktop */
static int s_resFitN = 0;
static int s_resSel  = 0;       /* index into s_resFit */

struct Row {
    int                page;
    const char        *key;
    const char        *label;
    int                kind;
    double             step;
    const char *const *names;    /* ROW_TOGGLE / ROW_ENUM value names */
    int                restart;  /* value change needs a restart      */
    double             uiMin, uiMax; /* 0,0 -> use the registered clamp */

    /* resolved from config.c at init */
    int                found;
    int                type;     /* CONFIG_OPT_*    */
    void              *ptr;
    double             cfgMin, cfgMax;

    /* Hidden while the option named here is nonzero (a manual % row
     * disappears while its "auto" toggle is on). Resolved to hidePtr at init. */
    const char        *hiddenIfOn;
    int               *hidePtr;
};

static struct Row rows[] = {
    /* Graphics */
    { PAGE_GRAPHICS, "__GraphicsPreset",          "Graphics preset",     ROW_ENUM,   1,    kGraphicsPreset, 0, 0,   4,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.Fullscreen",         "Fullscreen",          ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "__Resolution",             "Output resolution",   ROW_RES,    0,    NULL,       0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.RenderScale",        "Render resolution",   ROW_SLIDER, 25,   NULL,       0, 50, 200,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.VSync",              "VSync",               ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.FpsCap",             "Frame cap",           ROW_SLIDER, 10,   NULL,       0, 0, 360,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.MSAA",               "MSAA",                ROW_MSAA,   0,    NULL,       1, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.TAA",                "Temporal AA (TAA)",   ROW_ENUM,   1,    kTaaMode,   0, 0,   2,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.TextureFilter",      "Texture filter",      ROW_ENUM,   1,    kTexFilter,      0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.MipmapFilter",       "Mipmap filter",       ROW_ENUM,   1,    kMipmapFilter,   0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.FramebufferEffects", "Framebuffer effects", ROW_TOGGLE, 1,    kOnOff,          1, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "__ScreenMode",             "Game screen mode",    ROW_ENUM,   1,    kScreenMode,     0, 0,   2,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "__ScreenRatio",            "Game aspect ratio",   ROW_ENUM,   1,    kScreenRatio,    0, 0,   1,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.Anisotropy",         "Anisotropic filter",  ROW_SLIDER, 1,    NULL,       0, 1,  16,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.FixMipTextures",     "Mip texture fix",     ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.WrapFix",            "Texture wrap fix",    ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.FovScale",           "FOV scale",           ROW_SLIDER, 5,    NULL,       0, 50, 150,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.DrawDistanceAutoFov","Auto draw distance",  ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.DrawDistance",       "Draw distance",       ROW_SLIDER, 25,   NULL,       0,100, 400,   0,0,0,0,0, "Video.DrawDistanceAutoFov" },
    { PAGE_GRAPHICS, "Video.LodDistanceAutoFov", "Auto LOD distance",   ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GRAPHICS, "Video.LodDistance",        "LOD distance",        ROW_SLIDER, 25,   NULL,       0, 25, 400,   0,0,0,0,0, "Video.LodDistanceAutoFov" },

    /* Audio */
    { PAGE_AUDIO, "__AudioPreset",                "Latency preset",      ROW_ENUM,   1,    kAudioPreset, 0, 0, 3,   0,0,0,0,0 },
    { PAGE_AUDIO, "Audio.Mute",                  "Mute",                 ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_AUDIO, "Audio.MasterVolume",          "Master volume",       ROW_SLIDER, 5,    NULL,       0, 0, 100,   0,0,0,0,0 },
    { PAGE_AUDIO, "__MusicVolume",                "Music volume",        ROW_SLIDER, 5,    NULL,       0, 0, 100,   0,0,0,0,0 },
    { PAGE_AUDIO, "__SfxVolume",                  "SFX volume",          ROW_SLIDER, 5,    NULL,       0, 0, 100,   0,0,0,0,0 },
    { PAGE_AUDIO, "Audio.QueueLimit",            "Queue limit",         ROW_SLIDER, 128,  NULL,       0, 512,8192,  0,0,0,0,0 },
    { PAGE_AUDIO, "Audio.BufferSize",            "Device buffer",       ROW_SLIDER, 64,   NULL,       1, 128,4096,  0,0,0,0,0 },

    /* Input */
    { PAGE_INPUT, "Input.MouseEnabled",          "Mouse enabled",       ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.AimAbsolute",           "Absolute aim",        ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.AimModeSens",           "Aim sensitivity",     ROW_SLIDER, 1,    NULL,       0, 1,  80,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseTurnSpeed",        "Turn sensitivity",    ROW_SLIDER, 1,    NULL,       0, 1, 100,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.SensLink",              "Link sensitivities",  ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseInvertY",          "Invert mouse Y",      ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseSmoothing",        "Mouse smoothing",     ROW_SLIDER, 5,    NULL,       0, 0,  90,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseRawInput",         "Raw mouse input",     ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseDtDecouple",       "Frame-rate decouple", ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.AimBand",               "Aim edge band",      ROW_SLIDER, 1,    NULL,       0, 5,  40,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseSensitivity",      "Base mouse sens",    ROW_SLIDER, 5,    NULL,       0, 1, 500,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseAimCurve",         "Aim response curve", ROW_SLIDER, 10,   NULL,       0,50, 400,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MouseYScale",           "Mouse Y scale",      ROW_SLIDER, 5,    NULL,       0, 1, 500,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MenuPointerSpeed",      "Menu pointer speed", ROW_SLIDER, 10,   NULL,       0,10, 500,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.MenuPointerMode",       "1:1 menu pointer",    ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.HipfirePitchSpeed",     "Hipfire pitch speed",ROW_SLIDER, 10,   NULL,       0,10, 500,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.NaturalPitch",          "Natural pitch",       ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.PadDeadzone",           "Pad deadzone",        ROW_SLIDER, 1000, NULL,       0, 0,30000,  0,0,0,0,0 },
    { PAGE_INPUT, "Input.PadTriggerPct",         "Trigger threshold",   ROW_SLIDER, 5,    NULL,       0, 1,  99,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.PadLookInvertY",        "Invert pad Y",        ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.Forward",        "Bind forward",       ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.Back",           "Bind back",          ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.StrafeLeft",     "Bind strafe left",   ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.StrafeRight",    "Bind strafe right",  ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.TurnLeft",       "Bind turn left",     ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.TurnRight",      "Bind turn right",    ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.Fire",           "Bind fire key",      ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.Aim",            "Bind aim key",       ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.Action",         "Bind action",        ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.Cancel",         "Bind cancel",        ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.LeanLeft",       "Bind lean/Q",        ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },
    { PAGE_INPUT, "Input.Bind.Start",          "Bind start/pause",   ROW_BIND, 0, NULL, 0, 0,0, 0,0,0,0,0 },

    /* Gameplay */
    { PAGE_GAMEPLAY, "__AutoAim",                 "Auto-aim",             ROW_TOGGLE, 1,    kOnOff,      0, 0, 0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "__AimControl",              "Aim control",          ROW_ENUM,   1,    kAimControl,  0, 0, 1,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "__SightOnscreen",           "Crosshair / sight",    ROW_TOGGLE, 1,    kOnOff,      0, 0, 0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "__LookAhead",               "Look-ahead",           ROW_TOGGLE, 1,    kOnOff,      0, 0, 0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "__AmmoOnscreen",            "Ammo HUD",             ROW_TOGGLE, 1,    kOnOff,      0, 0, 0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "__LookInvert",              "N64 look inversion",   ROW_TOGGLE, 1,    kOnOff,      0, 0, 0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "Game.ScreenShakeIntensity","Screen shake",        ROW_SLIDER, 0.25, NULL,       0, 0, 2.0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "Game.SkipIntro",           "Skip intro",           ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "Game.NoHitFlash",          "Disable hit flash",   ROW_TOGGLE, 1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },
    { PAGE_GAMEPLAY, "Game.AllUnlocked",         "All missions unlocked",ROW_TOGGLE,1,    kOnOff,     0, 0,   0,   0,0,0,0,0 },

    /* Cheats — original GE handlers/state, not host-side imitations. */
    { PAGE_CHEATS, "__CheatInvincible",          "Invincibility",        ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatAllGuns",             "All guns",             ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatInfiniteAmmo",        "Infinite ammo",        ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatInvisible",           "Invisibility",         ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatDK",                  "DK mode",              ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatTiny",                "Tiny Bond",            ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatPaintball",           "Paintball mode",       ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatTurbo",               "Turbo mode",           ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatLineMode",            "Line mode",            ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatEnemyRockets",        "Enemy rockets",        ROW_TOGGLE, 1, kOnOff, 0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatMaxAmmo",             "Give max ammo",        ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatExtraWeapons",        "Give extra weapons",   ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatGoldenGun",           "Give Golden Gun",      ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatLaser",               "Give laser",           ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatMagnum",              "Give magnum",          ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__Cheat10xHealth",           "Give 10x health",      ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__Cheat2xRL",                "Dual rocket launchers",ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__Cheat2xGL",                "Dual grenade launchers",ROW_ACTION,0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__Cheat2xRCP90",             "Dual RCP90s",          ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },
    { PAGE_CHEATS, "__CheatClearAll",            "Disable all cheats",   ROW_ACTION, 0, NULL,   0, 0, 0, 0,0,0,0,0 },

    /* System / diagnostics. Governor/VM profiles are applied by the
     * PortMaster launcher on the next launch and restored on game exit. */
    { PAGE_SYSTEM, "Video.DisplayFPS",           "FPS-only counter",    ROW_TOGGLE, 1,    kOnOff,       0, 0,   0,   0,0,0,0,0 },
    { PAGE_SYSTEM, "Debug.PerfHUD",              "CPU/FPS/RAM HUD",     ROW_TOGGLE, 1,    kOnOff,       0, 0,   0,   0,0,0,0,0 },
    { PAGE_SYSTEM, "Debug.InputLog",             "Input logging",       ROW_TOGGLE, 1,    kOnOff,       0, 0,   0,   0,0,0,0,0 },
    { PAGE_SYSTEM, "System.CpuGovernor",         "CPU governor",        ROW_ENUM,   1,    kCpuGovernor, 1, 0,   3,   0,0,0,0,0 },
    { PAGE_SYSTEM, "System.GpuGovernor",         "GPU governor",        ROW_ENUM,   1,    kGpuGovernor, 1, 0,   3,   0,0,0,0,0 },
    { PAGE_SYSTEM, "System.RamProfile",          "RAM profile",         ROW_ENUM,   1,    kRamProfile,  1, 0,   2,   0,0,0,0,0 },
    { PAGE_SYSTEM, "__TrimRAM",                  "Trim allocator now",  ROW_ACTION, 0,    NULL,         0, 0,   0,   0,0,0,0,0 },
};
#define NUM_ROWS ((int)(sizeof(rows) / sizeof(rows[0])))

static int  s_inited = 0;
static volatile int s_open = 0;
static int  s_sel = 0;
static int  s_page = PAGE_GRAPHICS;        /* selection, index into s_visIdx (visible list) */

/* Visible-row list: rows whose hiddenIfOn option is nonzero are omitted
 * (manual % rows hide while their auto toggle is on). Rebuilt every frame in
 * overlayUpdateVisible(); s_scroll is the first visible-list entry drawn. */
static int  s_visIdx[NUM_ROWS];
static int  s_visN = 0;
static int  s_scroll = 0;
static int  s_graphicsPreset = 0;
static int  s_audioPreset = 0;
static int  s_presetDepth = 0;
static int  s_bindCaptureRow = -1;
static int  s_bindWaitRelease = 0;

/* D213: optional on-screen FPS readout (PD parity: Video.DisplayFPS). Drawn
 * top-right whenever enabled, independent of the F10 panel. Config-only knob
 * (kept off the 13-row panel, which is at its layout limit) -- matches PD,
 * whose DisplayFPS is also file-only. Default 0 => emit path unchanged =>
 * golden dumps byte-identical. */
static int      s_showFps = 0;
static char     s_fpsText[16] = "";

PD_CONSTRUCTOR static void overlayConfigInit(void)
{
    configRegisterInt("Video.DisplayFPS", &s_showFps, 0, 1);
}

/* Sampled once per emitted frame; recomputes the string every ~0.5 s. */
static void fpsTick(void)
{
    static uint64_t winStartUs = 0;
    static int      frames = 0;

    uint64_t nowUs = sysGetMicroseconds();
    if (winStartUs == 0) {
        winStartUs = nowUs;
        return;
    }
    frames++;
    uint64_t dtUs = nowUs - winStartUs;
    if (dtUs >= 500000) {
        int fps = (int)((double)frames * 1e6 / (double)dtUs + 0.5);
        snprintf(s_fpsText, sizeof(s_fpsText), "%d FPS", fps);
        winStartUs = nowUs;
        frames = 0;
    }
}

/* Layout (game 2D pixel space = viGetX() x viGetY(), ~320x240). Shared by the
 * emit path and the mouse hit-testing in optionsOverlayHandleInput().
 * BankGothic caps are ~9 units tall here, so rows need ~16 units of pitch and
 * values are right-aligned to the panel edge to survive the wide font. */
/* Resolution-scalable logical layout. GoldenEye's VI coordinate space
 * changes between gameplay/front-end modes, so all important edges derive
 * from the current logical viewport instead of assuming 320x240. */
#define OV_MARGIN_X  ((viGetX() >= 400) ? 12 : 7)
#define OV_MARGIN_Y  ((viGetY() >= 300) ? 8 : 5)
#define OV_TOP       (OV_MARGIN_Y + 3)
#define OV_TAB_Y     ((viGetY() * 10) / 100)
#define OV_BODY_Y    ((viGetY() * 18) / 100)
#define OV_LINE      ((viGetY() >= 300) ? 19 : 17)
#define OV_LABEL_X   (OV_MARGIN_X + ((viGetX() >= 400) ? 14 : 15))
#define OV_RIGHT     (viGetX() - OV_MARGIN_X - 7)
#define OV_BAR_X     ((viGetX() * 57) / 100)
#define OV_NUM_W     ((viGetX() >= 400) ? 58 : 46)
#define OV_ROW_Y(i)  (OV_BODY_Y + (i) * OV_LINE)

static int maxVisibleRows(void)
{
    int reserve = 24;
    if (s_page == PAGE_SYSTEM)
        reserve += (viGetY() >= 300) ? 76 : 64;
    else if (s_page == PAGE_AUDIO)
        reserve += 30;
    int n = (viGetY() - OV_BODY_Y - reserve) / OV_LINE;
    return n < 4 ? 4 : n;
}

static void sliderBarSpan(s32 *x0, s32 *x1)
{
    *x0 = OV_BAR_X;
    *x1 = OV_RIGHT - OV_NUM_W;
    if (*x1 < *x0 + 20) *x1 = *x0 + 20;
}

static int overlayRowAtY(double oy)
{
    for (int p = 0; p < s_visN; p++) {
        double top = OV_ROW_Y(p - s_scroll) - 3;
        if (oy >= top && oy < top + OV_LINE) return p;
    }
    return -1;
}

static void overlayUpdateVisible(void)
{
    s_visN = 0;
    for (int i = 0; i < NUM_ROWS; ++i) {
        if (rows[i].page != s_page) continue;
        if (rows[i].hidePtr && *rows[i].hidePtr != 0) continue;
        s_visIdx[s_visN++] = i;
    }

    if (s_visN == 0) {
        s_sel = 0;
        s_scroll = 0;
        return;
    }
    if (s_sel < 0) s_sel = 0;
    if (s_sel >= s_visN) s_sel = s_visN - 1;
}

static void overlayUpdateScroll(void)
{
    const int maxV = maxVisibleRows();
    if (s_visN <= maxV) {
        s_scroll = 0;
        return;
    }
    if (s_sel < s_scroll) s_scroll = s_sel;
    if (s_sel >= s_scroll + maxV) s_scroll = s_sel - maxV + 1;
    if (s_scroll < 0) s_scroll = 0;
    if (s_scroll > s_visN - maxV) s_scroll = s_visN - maxV;
}

static int tabAtX(double ox)
{
    const int W = viGetX();
    const int left = 14, right = W - 14;
    const int span = right - left;
    for (int i = 0; i < PAGE_COUNT; ++i) {
        int x0 = left + (span * i) / PAGE_COUNT;
        int x1 = left + (span * (i + 1)) / PAGE_COUNT;
        if (ox >= x0 && ox < x1) return i;
    }
    return -1;
}

static int overlayInCloseBox(double ox, double oy)
{
    return ox >= viGetX() - 30 && ox <= viGetX() - 8 && oy >= 5 && oy <= 21;
}

/* ------------------------------------------------------------------------ */

static void resolveCb(const char *key, int type, void *ptr, double min, double max,
                      double step, const char *label, const char *const *names,
                      void *ctx)
{
    (void)step; (void)label; (void)names; (void)ctx;
    for (int i = 0; i < NUM_ROWS; i++) {
        if (strcmp(rows[i].key, key) == 0) {
            rows[i].found  = 1;
            rows[i].type   = type;
            rows[i].ptr    = ptr;
            rows[i].cfgMin = min;
            rows[i].cfgMax = max;
            return;
        }
    }
}

static void overlayInit(void)
{
    if (s_inited) {
        return;
    }
    s_inited = 1;

    /* Publish display metadata so config.c / future consumers can see it,
     * without config.c knowing any specific key. */
    for (int i = 0; i < NUM_ROWS; i++) {
        configSetOptionMeta(rows[i].key, rows[i].label, rows[i].step, rows[i].names);
    }
    configForEachOption(resolveCb, NULL);

    for (int i = 0; i < NUM_ROWS; i++) {
        if (rows[i].kind == ROW_RES ||
            strcmp(rows[i].key, "__ScreenMode") == 0 ||
            strcmp(rows[i].key, "__ScreenRatio") == 0 ||
            strncmp(rows[i].key, "__", 2) == 0) {
            rows[i].found = 1;   /* special live rows, not config-backed */
            continue;
        }
        if (!rows[i].found) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: option '%s' not registered",
                         rows[i].key);
        }
    }

    /* Resolve the hidden-while-on sources (manual % rows vs their auto
     * toggles), then build the initial visible list. */
    for (int i = 0; i < NUM_ROWS; i++) {
        if (!rows[i].hiddenIfOn) {
            continue;
        }
        for (int j = 0; j < NUM_ROWS; j++) {
            if (strcmp(rows[j].key, rows[i].hiddenIfOn) == 0 && rows[j].found) {
                rows[i].hidePtr = rows[j].ptr;
                break;
            }
        }
    }
    overlayUpdateVisible();

    /* Build the windowed-resolution preset list: presets that fit the desktop,
     * plus the current window size snapped to the nearest surviving entry. */
    {
        int dw = 1920, dh = 1080;
        videoGetDesktopSize(&dw, &dh);
        s_resFitN = 0;
        for (int i = 0; i < NUM_RES; i++) {
            if (kResList[i][0] <= dw && kResList[i][1] <= dh) {
                s_resFit[s_resFitN++] = i;
            }
        }
        if (s_resFitN == 0) {
            s_resFit[s_resFitN++] = 0;
        }
        int cw = 0, ch = 0;
        videoGetWindowSize(&cw, &ch);
        long best = -1;
        for (int k = 0; k < s_resFitN; k++) {
            int i = s_resFit[k];
            long d = labs((long)kResList[i][0] - cw) +
                     labs((long)kResList[i][1] - ch);
            if (best < 0 || d < best) { best = d; s_resSel = k; }
        }
    }

    const char *e = getenv("GE_OPTIONSOVERLAY");
    if (e && atoi(e) != 0) {
        s_open = 1;
        sysLogPrintf(LOG_INFO, "optionsoverlay: auto-opened (GE_OPTIONSOVERLAY)");
    }
}

static int cheatIdForKey(const char *key);

static double rowGet(const struct Row *r)
{
    if (strcmp(r->key, "__ScreenMode") == 0) return (double)cur_player_get_screen_setting();
    if (strcmp(r->key, "__ScreenRatio") == 0) return (double)get_screen_ratio();
    if (strcmp(r->key, "__GraphicsPreset") == 0) return (double)s_graphicsPreset;
    if (strcmp(r->key, "__AudioPreset") == 0) return (double)s_audioPreset;
    if (strcmp(r->key, "__AutoAim") == 0) return (double)cur_player_get_autoaim();
    if (strcmp(r->key, "__AimControl") == 0) return (double)cur_player_get_aim_control();
    if (strcmp(r->key, "__SightOnscreen") == 0) return (double)cur_player_get_sight_onscreen_control();
    if (strcmp(r->key, "__LookAhead") == 0) return (double)cur_player_get_lookahead();
    if (strcmp(r->key, "__AmmoOnscreen") == 0) return (double)cur_player_get_ammo_onscreen_setting();
    if (strcmp(r->key, "__LookInvert") == 0) return (double)get_cur_player_look_vertical_inverted();
    if (strcmp(r->key, "__MusicVolume") == 0) return (double)get_mTrack2Vol() * 100.0 / 32767.0;
    if (strcmp(r->key, "__SfxVolume") == 0) return (double)call_sndGetSfxSlotFirstNaturalVolume() * 100.0 / 32767.0;
    {
        int cid = cheatIdForKey(r->key);
        if (cid != GE_CHEAT_UNUSED) return cheatIsActive(cid) ? 1.0 : 0.0;
    }
    if (!r->found || !r->ptr) {
        return 0.0;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:   return (double)*(int *)r->ptr;
    case CONFIG_OPT_UINT:  return (double)*(unsigned int *)r->ptr;
    case CONFIG_OPT_FLOAT: return (double)*(float *)r->ptr;
    default:               return 0.0;
    }
}

static double rowLo(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMin : r->cfgMin;
}
static double rowHi(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMax : r->cfgMax;
}

static struct Row *rowByKey(const char *key)
{
    for (int i = 0; i < NUM_ROWS; i++) {
        if (strcmp(rows[i].key, key) == 0) {
            return &rows[i];
        }
    }
    return NULL;
}

static int cheatIdForKey(const char *key)
{
    if (strcmp(key, "__CheatInvincible") == 0)   return GE_CHEAT_INVINCIBILITY;
    if (strcmp(key, "__CheatAllGuns") == 0)      return GE_CHEAT_ALLGUNS;
    if (strcmp(key, "__CheatInfiniteAmmo") == 0) return GE_CHEAT_INFINITE_AMMO;
    if (strcmp(key, "__CheatInvisible") == 0)    return GE_CHEAT_INVISIBILITY;
    if (strcmp(key, "__CheatDK") == 0)           return GE_CHEAT_DK_MODE;
    if (strcmp(key, "__CheatTiny") == 0)         return GE_CHEAT_TINY_BOND;
    if (strcmp(key, "__CheatPaintball") == 0)    return GE_CHEAT_PAINTBALL;
    if (strcmp(key, "__CheatTurbo") == 0)        return GE_CHEAT_TURBO_MODE;
    if (strcmp(key, "__CheatLineMode") == 0)     return GE_CHEAT_LINEMODE;
    if (strcmp(key, "__CheatEnemyRockets") == 0) return GE_CHEAT_ENEMY_ROCKETS;
    return GE_CHEAT_UNUSED;
}

static int s_linkDepth = 0;   /* re-entrancy guard for the sens link below */

static void rowSet(struct Row *r, double v)
{
    double lo = rowLo(r), hi = rowHi(r);

    /* Presets are conveniences, not hidden state. Any manual tweak to a
     * preset-owned setting immediately returns the label to CUSTOM. */
    if (!s_presetDepth) {
        if (strncmp(r->key, "Video.", 6) == 0 &&
            strcmp(r->key, "Video.DisplayFPS") != 0) {
            s_graphicsPreset = 0;
        }
        if (strcmp(r->key, "Audio.QueueLimit") == 0 ||
            strcmp(r->key, "Audio.BufferSize") == 0) {
            s_audioPreset = 0;
        }
    }
    if (strcmp(r->key, "__ScreenMode") == 0) {
        if (v < 0) v = 0; if (v > 2) v = 2;
        cur_player_set_screen_setting((u32)lround(v));
        return;
    }
    if (strcmp(r->key, "__ScreenRatio") == 0) {
        if (v < 0) v = 0; if (v > 1) v = 1;
        set_screen_ratio((u32)lround(v));
        return;
    }

    /* Native GoldenEye options: direct calls into the game's own settings. */
    if (strcmp(r->key, "__AutoAim") == 0) {
        cur_player_set_autoaim((u32)(v != 0.0)); return;
    }
    if (strcmp(r->key, "__AimControl") == 0) {
        if (v < 0) v = 0; if (v > 1) v = 1;
        cur_player_set_aim_control((u32)lround(v)); return;
    }
    if (strcmp(r->key, "__SightOnscreen") == 0) {
        cur_player_set_sight_onscreen_control((u32)(v != 0.0)); return;
    }
    if (strcmp(r->key, "__LookAhead") == 0) {
        cur_player_set_lookahead((u32)(v != 0.0)); return;
    }
    if (strcmp(r->key, "__AmmoOnscreen") == 0) {
        cur_player_set_ammo_onscreen_setting((u32)(v != 0.0)); return;
    }
    if (strcmp(r->key, "__LookInvert") == 0) {
        set_cur_player_look_vertical_inverted((u32)(v != 0.0)); return;
    }
    if (strcmp(r->key, "__MusicVolume") == 0) {
        if (v < 0) v = 0; if (v > 100) v = 100;
        set_mTrack2Vol((u16)lround(v * 32767.0 / 100.0));
        return;
    }
    if (strcmp(r->key, "__SfxVolume") == 0) {
        if (v < 0) v = 0; if (v > 100) v = 100;
        sub_GAME_7F0A91A0((u16)lround(v * 32767.0 / 100.0));
        return;
    }

    if (strcmp(r->key, "__TrimRAM") == 0) {
        int released = systemPerfTrimMemory();
        sysLogPrintf(LOG_INFO, "optionsoverlay: allocator trim %s",
                     released ? "released pages" : "completed/no pages released");
        return;
    }

    /* Original GoldenEye cheat machinery. */
    if (strcmp(r->key, "__CheatMaxAmmo") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_MAXAMMO);
        return;
    }
    if (strcmp(r->key, "__CheatExtraWeapons") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_EXTRA_WEAPONS); return;
    }
    if (strcmp(r->key, "__CheatGoldenGun") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_GOLDEN_GUN); return;
    }
    if (strcmp(r->key, "__CheatLaser") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_LASER); return;
    }
    if (strcmp(r->key, "__CheatMagnum") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_MAGNUM); return;
    }
    if (strcmp(r->key, "__Cheat10xHealth") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_10X_HEALTH); return;
    }
    if (strcmp(r->key, "__Cheat2xRL") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_2X_ROCKET_LAUNCHER); return;
    }
    if (strcmp(r->key, "__Cheat2xGL") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_2X_GRENADE_LAUNCHER); return;
    }
    if (strcmp(r->key, "__Cheat2xRCP90") == 0) {
        cheatButtonTurnOnCheatForPlayers(GE_CHEAT_2X_RCP90); return;
    }
    if (strcmp(r->key, "__CheatClearAll") == 0) {
        cheatDisableAllCheats();
        {
            const int extra[] = { GE_CHEAT_INVINCIBILITY, GE_CHEAT_ALLGUNS,
                GE_CHEAT_DK_MODE, GE_CHEAT_TINY_BOND, GE_CHEAT_ENEMY_ROCKETS };
            for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); ++i) {
                if (cheatIsActive(extra[i])) cheatButtonHandleCheatsTurnedOff(extra[i]);
            }
        }
        return;
    }
    {
        int cid = cheatIdForKey(r->key);
        if (cid != GE_CHEAT_UNUSED) {
            int want = v != 0.0;
            int have = cheatIsActive(cid) ? 1 : 0;
            if (want != have) {
                if (want) cheatButtonTurnOnCheatForPlayers(cid);
                else cheatButtonHandleCheatsTurnedOff(cid);
            }
            return;
        }
    }

    /* Presets only write normal settings through rowSet, so all existing
     * clamping/live-apply behavior stays centralized. */
    if (strcmp(r->key, "__GraphicsPreset") == 0) {
        int p = (int)lround(v);
        if (p < 0) p = 0; if (p > 4) p = 4;
        s_graphicsPreset = p;
        if (p != 0) {
            struct Row *x;
            s_presetDepth++;
#define PRESET_SET(k,val) do { x=rowByKey(k); if (x && x->found) rowSet(x,(val)); } while(0)
            if (p == 1) {
                PRESET_SET("Video.TextureFilter", 0);
                PRESET_SET("Video.MipmapFilter", 1);
                PRESET_SET("Video.Anisotropy", 1);
                PRESET_SET("Video.FovScale", 100);
                PRESET_SET("Video.DrawDistanceAutoFov", 0);
                PRESET_SET("Video.DrawDistance", 100);
                PRESET_SET("Video.LodDistanceAutoFov", 0);
                PRESET_SET("Video.LodDistance", 100);
            } else if (p == 2) {
                PRESET_SET("Video.TextureFilter", 0);
                PRESET_SET("Video.MipmapFilter", 0);
                PRESET_SET("Video.Anisotropy", 1);
                PRESET_SET("Video.FovScale", 100);
                PRESET_SET("Video.DrawDistanceAutoFov", 0);
                PRESET_SET("Video.DrawDistance", 150);
                PRESET_SET("Video.LodDistanceAutoFov", 0);
                PRESET_SET("Video.LodDistance", 150);
            } else if (p == 3) {
                PRESET_SET("Video.TextureFilter", 2);
                PRESET_SET("Video.MipmapFilter", 2);
                PRESET_SET("Video.Anisotropy", 8);
                PRESET_SET("Video.FovScale", 115);
                PRESET_SET("Video.DrawDistanceAutoFov", 1);
                PRESET_SET("Video.LodDistanceAutoFov", 0);
                PRESET_SET("Video.LodDistance", 200);
            } else if (p == 4) {
                PRESET_SET("Video.TextureFilter", 1);
                PRESET_SET("Video.MipmapFilter", 2);
                PRESET_SET("Video.Anisotropy", 2);
                PRESET_SET("Video.FovScale", 100);
                PRESET_SET("Video.DrawDistanceAutoFov", 0);
                PRESET_SET("Video.DrawDistance", 125);
                PRESET_SET("Video.LodDistanceAutoFov", 0);
                PRESET_SET("Video.LodDistance", 100);
            }
#undef PRESET_SET
            s_presetDepth--;
        }
        return;
    }
    if (strcmp(r->key, "__AudioPreset") == 0) {
        int p = (int)lround(v);
        if (p < 0) p = 0; if (p > 3) p = 3;
        s_audioPreset = p;
        if (p != 0) {
            struct Row *q = rowByKey("Audio.QueueLimit");
            struct Row *b = rowByKey("Audio.BufferSize");
            s_presetDepth++;
            if (p == 1) { if (q&&q->found) rowSet(q,1536); if (b&&b->found) rowSet(b,256); }
            if (p == 2) { if (q&&q->found) rowSet(q,2880); if (b&&b->found) rowSet(b,512); }
            if (p == 3) { if (q&&q->found) rowSet(q,4096); if (b&&b->found) rowSet(b,1024); }
            s_presetDepth--;
        }
        return;
    }

    if (lo != hi) {
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:   *(int *)r->ptr = (int)lround(v); break;
    case CONFIG_OPT_UINT:  *(unsigned int *)r->ptr = (unsigned int)(v < 0 ? 0 : lround(v)); break;
    case CONFIG_OPT_FLOAT: *(float *)r->ptr = (float)v; break;
    default: return;
    }

    /* Live-apply the video knobs that need a fast3d/SDL call. Everything else
     * is read straight off the pointer by its owner every frame/poll. */
    if (strcmp(r->key, "Video.Fullscreen") == 0) {
        videoRequestFullscreen((int)lround(v));
    } else if (strncmp(r->key, "Video.", 6) == 0 && !r->restart) {
        videoRequestLiveConfig();
    }

    /* Linked aim/turn sensitivity (Input.SensLink, default on): moving either
     * knob scales the other to hold the stock default ratio -- AimModeSens 38
     * : MouseTurnSpeed 50 (the D194/D238 calibrated defaults). */
    if (!s_linkDepth && strcmp(r->key, "Input.SensLink") != 0) {
        struct Row *lk = rowByKey("Input.SensLink");
        if (lk && lk->found && *(int *)lk->ptr != 0) {
            struct Row *o = NULL;
            double nv = 0.0;
            if (strcmp(r->key, "Input.MouseTurnSpeed") == 0) {
                o = rowByKey("Input.AimModeSens");
                nv = v * 38.0 / 50.0;
            } else if (strcmp(r->key, "Input.AimModeSens") == 0) {
                o = rowByKey("Input.MouseTurnSpeed");
                nv = v * 50.0 / 38.0;
            }
            if (o && o->found) {
                s_linkDepth = 1;
                rowSet(o, nv);
                s_linkDepth = 0;
            }
        }
    }
}

static void rowAdjust(struct Row *r, int dir)
{
    if (!r->found) {
        return;
    }
    double v = rowGet(r);
    switch (r->kind) {
    case ROW_TOGGLE:
        rowSet(r, (v != 0.0) ? 0.0 : 1.0);
        break;
    case ROW_MSAA: {
        int idx = 0;
        for (int i = 0; i < 4; i++) {
            if (kMsaaSeq[i] == (int)lround(v)) idx = i;
        }
        /* GLES3 Fast3D owns a real multisample FBO/resolve path. The backend
         * clamps the chosen value to GL_MAX_SAMPLES at allocation time. */
        idx = (idx + dir + 4) % 4;
        rowSet(r, (double)kMsaaSeq[idx]);
        break;
    }
    case ROW_ENUM: {
        double lo = rowLo(r), hi = rowHi(r);
        v += dir;
        if (v < lo) v = hi;
        if (v > hi) v = lo;
        rowSet(r, v);
        break;
    }
    case ROW_ACTION:
        rowSet(r, 1.0);
        break;
    case ROW_BIND:
        s_bindCaptureRow = (int)(r - rows);
        s_bindWaitRelease = 1;
        break;
    case ROW_RES: {
        if (s_resFitN <= 0 || videoIsFullscreen()) {
            break;   /* resolution is windowed-only */
        }
        s_resSel += (dir >= 0) ? 1 : -1;
        if (s_resSel < 0) s_resSel = s_resFitN - 1;
        if (s_resSel >= s_resFitN) s_resSel = 0;
        int i = s_resFit[s_resSel];
        videoRequestWindowSize(kResList[i][0], kResList[i][1]);
        break;
    }
    default: /* ROW_SLIDER */
        rowSet(r, v + dir * r->step);
        break;
    }
}

/* ------------------------------------------------------------------------ */

void optionsOverlayToggle(void)
{
    overlayInit();
    s_open = !s_open;
    sysLogPrintf(LOG_INFO, "optionsoverlay: %s", s_open ? "opened" : "closed");
    if (!s_open) {
        s_bindCaptureRow = -1;
        s_bindWaitRelease = 0;
        configSave();
    }
}

int optionsOverlayIsOpen(void)
{
    if (!s_inited) {
        overlayInit();
    }
    return s_open;
}

static void overlaySetPage(int page)
{
    if (page < 0) page = PAGE_COUNT - 1;
    if (page >= PAGE_COUNT) page = 0;
    if (page == s_page) return;
    s_page = page;
    s_sel = 0;
    s_scroll = 0;
    overlayUpdateVisible();
}

void optionsOverlayScroll(int dir)
{
    if (!s_inited) {
        overlayInit();
    }
    if (!s_open || dir == 0) {
        return;
    }
    overlayUpdateVisible();
    s_sel += (dir > 0) ? -1 : 1;   /* wheel-up -> move up the list */
    /* Clamp at both ends like a normal PC settings list -- wrapping made the
     * selection "repeat" from the far edge, which read as a duplicate. */
    if (s_sel < 0) s_sel = 0;
    if (s_sel >= s_visN) s_sel = s_visN - 1;
}

/* Set a slider row from an overlay-space x inside its value bar, snapped to
 * the row's step. */
static void sliderSetFromX(struct Row *r, double ox)
{
    double lo = rowLo(r), hi = rowHi(r);
    if (hi <= lo) {
        return;
    }
    s32 bx0, bx1;
    sliderBarSpan(&bx0, &bx1);
    double f = (ox - bx0) / (double)(bx1 - bx0);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    double v = lo + f * (hi - lo);
    double step = (r->step > 0.0) ? r->step : 1.0;
    v = lround(v / step) * step;
    rowSet(r, v);
}

void optionsOverlayHandleInput(void)
{
    static int prevUp, prevDn, prevLf, prevRt, prevLmb, prevRmb;
    static int dragRow = -1;

    if (!s_open) {
        prevUp = prevDn = prevLf = prevRt = prevLmb = prevRmb = 0;
        dragRow = -1;
        return;
    }

    /* The overlay owns the mouse while it is open: force the OS cursor free +
     * visible (a stage poll would otherwise leave it locked/hidden). */
    inputSuspendForOverlay();

    overlayUpdateVisible();   /* % rows may have appeared/vanished (auto toggles) */
    overlayUpdateScroll();

    const Uint8 *ks = SDL_GetKeyboardState(NULL);

    /* Live keyboard-bind capture. Enter/A starts capture on a bind row; once
     * the initiating key is released, the next keyboard scancode replaces
     * that action's primary bind and the existing input parser is rebuilt.
     * Escape cancels. F10 remains reserved for the overlay itself. */
    if (s_bindCaptureRow >= 0) {
        int any = 0;
        for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc) {
            if (ks[sc]) { any = 1; break; }
        }
        if (s_bindWaitRelease) {
            if (!any) s_bindWaitRelease = 0;
        } else {
            if (ks[SDL_SCANCODE_ESCAPE]) {
                s_bindCaptureRow = -1;
            } else {
                for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc) {
                    if (!ks[sc] || sc == SDL_SCANCODE_F10) continue;
                    struct Row *br = &rows[s_bindCaptureRow];
                    if (br->ptr && br->type == CONFIG_OPT_STR) {
                        const char *name = SDL_GetScancodeName((SDL_Scancode)sc);
                        if (name && *name) {
                            strncpy((char *)br->ptr, name, 63);
                            ((char *)br->ptr)[63] = 0;
                            inputRefreshBinds();
                            configSave();
                        }
                    }
                    s_bindCaptureRow = -1;
                    break;
                }
            }
        }
        /* While listening, swallow normal menu navigation so the captured key
         * cannot also move the selection or change another option. */
        if (s_bindCaptureRow >= 0) return;
    }

    int mx = 0, my = 0;
    Uint32 mb = SDL_GetMouseState(&mx, &my);
    int lmb = (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  ? 1 : 0;
    int rmb = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0;

    /* Gamepad navigation (controller-only machines, e.g. Steam Deck): the
     * D-pad or left stick moves the selection; A/X step forward (the Enter
     * equivalent), B/Y step back; Start closes. OR-ed into the same edge
     * logic as the keyboard, so repeat/clamp/scroll behaviour is identical.
     * input.c swallows the pad while we are open, so none of this reaches
     * the game. (Select also closes -- handled in input.c's toggle, which
     * runs before this one.) */
    int gUp = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_UP)
           || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) < -12000;
    int gDn = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
           || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) > 12000;
    int up = ks[SDL_SCANCODE_UP]    || ks[SDL_SCANCODE_KP_8] || gUp;
    int dn = ks[SDL_SCANCODE_DOWN]  || ks[SDL_SCANCODE_KP_2] || gDn;
    int lf = ks[SDL_SCANCODE_LEFT]  || ks[SDL_SCANCODE_KP_4]
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_B)
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_Y);
    int rt = ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_KP_6]
          || ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_A)
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_X);

    static int prevPageL = 0, prevPageR = 0;
    int pageL = ks[SDL_SCANCODE_PAGEUP] || ks[SDL_SCANCODE_Q]
             || inputPadButton(0, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    int pageR = ks[SDL_SCANCODE_PAGEDOWN] || ks[SDL_SCANCODE_E]
             || inputPadButton(0, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (pageL && !prevPageL) overlaySetPage(s_page - 1);
    if (pageR && !prevPageR) overlaySetPage(s_page + 1);
    prevPageL = pageL;
    prevPageR = pageR;

    static int prevStart = 0;   /* not reset while closed: a Start held across
                                  close must not re-close on the next open */
    int startNow = inputPadButton(0, SDL_CONTROLLER_BUTTON_START);
    if (startNow && !prevStart) optionsOverlayToggle();   /* Start closes */
    prevStart = startNow;

    /* ---- keyboard / D-pad nav (clamped at the ends; scroll follows) ---- */
    if (up && !prevUp && s_sel > 0) s_sel--;
    if (dn && !prevDn && s_sel < s_visN - 1) s_sel++;
    overlayUpdateScroll();
    if (lf && !prevLf) rowAdjust(&rows[s_visIdx[s_sel]], -1);
    if (rt && !prevRt) rowAdjust(&rows[s_visIdx[s_sel]], +1);

    /* ---- mouse ---- */
    int ww = 0, wh = 0;
    videoGetWindowSize(&ww, &wh);
    if (ww > 0 && wh > 0) {
        double ox = (double)mx * (double)viGetX() / ww;
        double oy = (double)my * (double)viGetY() / wh;
        int hoverVis = overlayRowAtY(oy);
        int onClose  = overlayInCloseBox(ox, oy);
        int hoverTab = (oy >= OV_TAB_Y - 3 && oy <= OV_TAB_Y + 13) ? tabAtX(ox) : -1;

        /* No hover-to-highlight: merely moving the mouse must not move the
         * selection or scroll the window (hovering near a list edge fed the
         * new row back into the cursor and made the bottom twitch/echo).
         * Selection moves only by click, wheel, or arrows. */

        /* left press. A click in the label column only focuses the row; a
         * click in the value/control column (>= the bar-span start) changes
         * it -- so clicking to select a toggle doesn't also flip it. */
        s32 bx0, bx1;
        sliderBarSpan(&bx0, &bx1);
        if (lmb && !prevLmb) {
            if (hoverTab >= 0) {
                overlaySetPage(hoverTab);
                prevLmb = lmb;
                return;
            }
            if (onClose) {
                optionsOverlayToggle();   /* close + configSave */
                return;
            }
            if (hoverVis >= 0) {
                s_sel = hoverVis;         /* explicit click -> select */
                overlayUpdateScroll();
                if (ox >= bx0) {
                    struct Row *r = &rows[s_visIdx[hoverVis]];
                    if (r->kind == ROW_SLIDER && r->found) {
                        sliderSetFromX(r, ox);
                        dragRow = s_visIdx[hoverVis];
                    } else {
                        rowAdjust(r, +1);   /* toggle / cycle forward (wraps) */
                    }
                }
            }
        }
        /* drag a slider */
        if (lmb && dragRow >= 0 && rows[dragRow].kind == ROW_SLIDER) {
            sliderSetFromX(&rows[dragRow], ox);
        }
        if (!lmb) {
            dragRow = -1;
        }
        /* right press in the value column: cycle back / decrement */
        if (rmb && !prevRmb && hoverVis >= 0 && !onClose && ox >= bx0) {
            s_sel = hoverVis;
            overlayUpdateScroll();
            rowAdjust(&rows[s_visIdx[hoverVis]], -1);
        }
    }

    prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
    prevLmb = lmb; prevRmb = rmb;
}

/* ------------------------------------------------------------------------ */

#define OV_BUF_CMDS 8192
static Gfx s_buf[OV_BUF_CMDS];

static Gfx *fillRect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1,
                     u8 r, u8 g, u8 b, u8 a)
{
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static void valueText(const struct Row *r, char *out, int n)
{
    double v = rowGet(r);
    if (r->kind == ROW_ACTION) {
        snprintf(out, n, "RUN");
        return;
    }
    if (r->kind == ROW_BIND) {
        if (s_bindCaptureRow >= 0 && &rows[s_bindCaptureRow] == r) {
            snprintf(out, n, "PRESS KEY...");
        } else if (r->ptr && r->type == CONFIG_OPT_STR) {
            const char *src = (const char *)r->ptr;
            const char *comma = strchr(src, ',');
            int len = comma ? (int)(comma - src) : (int)strlen(src);
            if (len > n - 1) len = n - 1;
            memcpy(out, src, (size_t)len);
            out[len] = 0;
        } else {
            snprintf(out, n, "UNBOUND");
        }
        return;
    }
    if (r->kind == ROW_RES) {
        if (videoIsFullscreen()) {
            snprintf(out, n, "(fullscreen)");
        } else if (s_resFitN <= 0) {
            snprintf(out, n, "n/a");
        } else {
            int i = s_resFit[s_resSel];
            snprintf(out, n, "%d x %d", kResList[i][0], kResList[i][1]);
        }
        return;
    }
    if ((r->kind == ROW_TOGGLE || r->kind == ROW_ENUM) && r->names) {
        int idx = (int)lround(v);
        int cnt = 0;
        while (r->names[cnt]) cnt++;
        if (idx >= 0 && idx < cnt) {
            snprintf(out, n, "%s", r->names[idx]);
            return;
        }
    }
    if (r->kind == ROW_MSAA) {
        if ((int)lround(v) <= 1) snprintf(out, n, "OFF");
        else                     snprintf(out, n, "%dx", (int)lround(v));
        return;
    }
    if (strcmp(r->key, "__MusicVolume") == 0 || strcmp(r->key, "__SfxVolume") == 0 ||
        strcmp(r->key, "Video.RenderScale") == 0 || strcmp(r->key, "Video.FovScale") == 0 ||
        strcmp(r->key, "Video.DrawDistance") == 0 || strcmp(r->key, "Video.LodDistance") == 0) {
        snprintf(out, n, "%d%%", (int)lround(v));
        return;
    }
    if (r->kind == ROW_SLIDER && r->type == CONFIG_OPT_FLOAT) {
        snprintf(out, n, "%.2f", v);
        return;
    }
    if (r->kind == ROW_SLIDER && (int)lround(v) == 0 &&
        strcmp(r->key, "Video.FpsCap") == 0) {
        snprintf(out, n, "OFF");
        return;
    }
    snprintf(out, n, "%d", (int)lround(v));
}

static Gfx *drawText(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour)
{
    s32 px = x, py = y;
    /* width/height are the on-screen CLIP rect textRenderGlyph tests against
     * (clipX=start x, clipY=start y, +clipWidth/+clipHeight), NOT the text's
     * own measured size -- passing the measured w/h clipped every glyph out
     * (baseline+height > measured h => nothing drawn).  Match the game: pass
     * the full 2D viewport, like bondview2.c's debug-text path. */
    return textRender(gdl, &px, &py, (char *)str, ptrFontBankGothicChars,
                      ptrFontBankGothic, colour, viGetX(), viGetY(), 0, 0);
}

static s32 measureText(const char *str)
{
    s32 h = 0, w = 0;
    textMeasure(&h, &w, (char *)str, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

/* Right-aligned: the string ends at xr. */
static Gfx *drawTextR(Gfx *gdl, s32 xr, s32 y, const char *str, u32 colour)
{
    return drawText(gdl, xr - measureText(str), y, str, colour);
}

Gfx *optionsOverlayEmit(void)
{
    if (!s_inited) overlayInit();
    fpsTick();

    if (!s_open) {
        if (!s_showFps || !s_fpsText[0]) return NULL;
        const s32 fw = viGetX(), fh = viGetY();
        Gfx *gdl = s_buf;
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetTexturePersp(gdl++, G_TP_NONE);
        gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, fw, fh);
        gdl = microcode_constructor(gdl);
        gdl = drawTextR(gdl, fw - 7, 6, s_fpsText, 0x62f4c7ff);
        gDPPipeSync(gdl++);
        gSPEndDisplayList(gdl++);
        return s_buf;
    }

    overlayUpdateVisible();
    overlayUpdateScroll();

    const s32 W = viGetX(), H = viGetY();
    const int maxV = maxVisibleRows();
    const int count = s_visN - s_scroll;
    const int drawN = count < maxV ? count : maxV;
    const int pLast = s_scroll + drawN - 1;
    s32 bx0, bx1;
    sliderBarSpan(&bx0, &bx1);

    Gfx *gdl = s_buf;
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, W, H);

    /* Fullscreen glass shell. Dark neutral base + cyan/gold accents keeps the
     * UI visually separate from GoldenEye's original watch/front-end art. */
    gdl = fillRect(gdl, 0, 0, W, H, 0, 0, 0, 150);
    gdl = fillRect(gdl, 7, 5, W - 8, H - 7, 9, 14, 20, 238);
    gdl = fillRect(gdl, 7, 5, W - 8, 22, 15, 25, 34, 250);
    gdl = fillRect(gdl, 7, 40, W - 8, 41, 70, 215, 190, 235);
    gdl = fillRect(gdl, 7, H - 22, W - 8, H - 7, 12, 20, 28, 248);

    /* Top tab strip. */
    const int tabLeft = 14, tabRight = W - 14, tabSpan = tabRight - tabLeft;
    for (int i = 0; i < PAGE_COUNT; ++i) {
        int x0 = tabLeft + (tabSpan * i) / PAGE_COUNT;
        int x1 = tabLeft + (tabSpan * (i + 1)) / PAGE_COUNT - 2;
        if (i == s_page)
            gdl = fillRect(gdl, x0, OV_TAB_Y - 3, x1, OV_TAB_Y + 12, 28, 90, 92, 245);
        else
            gdl = fillRect(gdl, x0, OV_TAB_Y - 3, x1, OV_TAB_Y + 12, 17, 31, 42, 225);
    }

    /* Rows as separated cards instead of one flat list. */
    for (int p = s_scroll; p <= pLast; ++p) {
        const struct Row *r = &rows[s_visIdx[p]];
        int y = OV_ROW_Y(p - s_scroll);
        const int selected = (p == s_sel);
        gdl = fillRect(gdl, 14, y - 3, W - 14, y + 11,
                       selected ? 27 : 15, selected ? 51 : 27,
                       selected ? 58 : 35, selected ? 245 : 220);
        if (selected)
            gdl = fillRect(gdl, 14, y - 3, 17, y + 11, 91, 241, 201, 255);

        if (r->kind == ROW_SLIDER && r->found) {
            double lo=rowLo(r), hi=rowHi(r);
            double f=(hi>lo)?(rowGet(r)-lo)/(hi-lo):0.0;
            if(f<0)f=0; if(f>1)f=1;
            gdl=fillRect(gdl,bx0,y+3,bx1,y+6,44,55,64,255);
            gdl=fillRect(gdl,bx0,y+3,bx0+(s32)((bx1-bx0)*f),y+6,87,231,193,255);
        }
    }

    gdl = microcode_constructor(gdl);

    gdl = drawText(gdl, 14, 8, "ARM-GE // PORT CONTROL", 0x62f4c7ff);
    gdl = drawTextR(gdl, W - 12, 8, "F10  X", 0xaebbc4ff);

    for (int i = 0; i < PAGE_COUNT; ++i) {
        int x0 = tabLeft + (tabSpan * i) / PAGE_COUNT;
        int x1 = tabLeft + (tabSpan * (i + 1)) / PAGE_COUNT - 2;
        int tw = measureText(kPageNames[i]);
        gdl = drawText(gdl, x0 + ((x1 - x0) - tw) / 2, OV_TAB_Y,
                       kPageNames[i], i == s_page ? 0xffffffff : 0x8fa3adff);
    }

    for (int p = s_scroll; p <= pLast; ++p) {
        const struct Row *r=&rows[s_visIdx[p]];
        int y=OV_ROW_Y(p-s_scroll);
        u32 col=(p==s_sel)?0xffffffff:0xc5d0d6ff;
        char val[64];
        gdl=drawText(gdl,OV_LABEL_X,y,r->label,r->found?col:0x66727aff);
        if(!r->found){
            gdl=drawTextR(gdl,OV_RIGHT,y,"N/A",0x66727aff);
            continue;
        }
        valueText(r,val,sizeof(val));
        if(r->restart){
            gdl=drawTextR(gdl,OV_RIGHT,y,"RESTART",0xd7a758ff);
            gdl=drawText(gdl,bx0,y,val,col);
        }else{
            gdl=drawTextR(gdl,OV_RIGHT,y,val,col);
        }
    }

    /* Page-specific live instrumentation. These panels are deliberately
     * read-only: they expose runtime health without adding another state
     * machine or allowing diagnostics to mutate game behavior. */
    if (s_page == PAGE_AUDIO) {
        const int q = audioGetSamplesBuffered();
        const int qMax = 2880; /* fresh-config target from audio.c */
        double qf = qMax > 0 ? (double)q / (double)qMax : 0.0;
        if (qf < 0.0) qf = 0.0;
        if (qf > 1.0) qf = 1.0;
        const int y0 = OV_BODY_Y + 5 * OV_LINE;
        if (y0 + 30 < H - 22) {
            char qtxt[64];
            gdl = fillRect(gdl, 14, y0, W - 14, y0 + 26, 12, 24, 31, 230);
            gdl = fillRect(gdl, 20, y0 + 15, W - 20, y0 + 19, 39, 50, 58, 255);
            gdl = fillRect(gdl, 20, y0 + 15,
                           20 + (s32)((W - 40) * qf), y0 + 19,
                           qf > 0.85 ? 217 : 91,
                           qf > 0.85 ? 167 : 241,
                           qf > 0.85 ? 88 : 201, 255);
            snprintf(qtxt, sizeof(qtxt), "QUEUE %d / %d FRAMES", q, qMax);
            gdl = microcode_constructor(gdl);
            gdl = drawText(gdl, 20, y0 + 3, qtxt,
                           qf > 0.85 ? 0xe2b768ff : 0x8cebd1ff);
        }
    }
    else if (s_page == PAGE_SYSTEM) {
        const DamLabSnapshot *d = damLabGetSnapshot();
        const int y0 = OV_BODY_Y + 2 * OV_LINE;
        if (y0 + 78 < H - 22) {
            char a[96], b[96], c[96], e[96];
            double cpu = d ? d->cpu_percent : 0.0;
            if (cpu < 0.0) cpu = 0.0;
            if (cpu > 100.0) cpu = 100.0;

            gdl = fillRect(gdl, 14, y0, W - 14, y0 + 74, 10, 22, 29, 235);
            gdl = fillRect(gdl, 20, y0 + 18, W - 20, y0 + 21, 38, 49, 57, 255);
            gdl = fillRect(gdl, 20, y0 + 18,
                           20 + (s32)((W - 40) * cpu / 100.0), y0 + 21,
                           cpu > 90.0 ? 217 : 91,
                           cpu > 90.0 ? 167 : 241,
                           cpu > 90.0 ? 88 : 201, 255);

            snprintf(a, sizeof(a), "CPU %3.0f%%   FPS %3.0f   AUDIO Q %d",
                     d ? d->cpu_percent : 0.0,
                     d ? d->fps : 0.0,
                     audioGetSamplesBuffered());
            snprintf(b, sizeof(b), "RAM %luM   FREE %luM",
                     d ? d->rss_kb / 1024ul : 0ul,
                     d ? d->mem_available_kb / 1024ul : 0ul);
            snprintf(c, sizeof(c), "STAGE %d  ROOM %d  STAN %s",
                     d ? d->stage : -1,
                     d ? d->room : -1,
                     (d && d->stan) ? "OK" : "NULL");
            snprintf(e, sizeof(e), "POS %d %d %d   FLAGS %02X",
                     d ? (int)d->pos_x : 0,
                     d ? (int)d->pos_y : 0,
                     d ? (int)d->pos_z : 0,
                     d ? (unsigned)(d->anomaly_flags & 0xffu) : 0u);

            gdl = microcode_constructor(gdl);
            gdl = drawText(gdl, 20, y0 + 3, "LIVE RUNTIME", 0x62f4c7ff);
            gdl = drawText(gdl, 20, y0 + 26, a, 0xd8e4e9ff);
            gdl = drawText(gdl, 20, y0 + 38, b, 0xb7c8d0ff);
            gdl = drawText(gdl, 20, y0 + 50, c,
                           (d && d->stan) ? 0x8cebd1ff : 0xe2b768ff);
            gdl = drawText(gdl, 20, y0 + 62, e,
                           (d && d->anomaly_flags) ? 0xe2b768ff : 0x91f1c9ff);
        }
    }

    {
        char perf[112];
        snprintf(perf,sizeof(perf),"%s   Q %d   %s",
                 s_fpsText[0]?s_fpsText:"-- FPS",
                 audioGetSamplesBuffered(), kPageHints[s_page]);
        gdl=drawText(gdl,14,H-18,perf,0x7fdcc4ff);
        gdl=drawTextR(gdl,W-14,H-18,"LB/RB TAB   F10 CLOSE",0x7f929cff);
    }

    gDPPipeSync(gdl++);
    gSPEndDisplayList(gdl++);
    if ((gdl-s_buf)>OV_BUF_CMDS)
        sysLogPrintf(LOG_ERROR,"optionsoverlay: DL overflow (%d)",(int)(gdl-s_buf));
    return s_buf;
}


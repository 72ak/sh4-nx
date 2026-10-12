/* Hand-written replacements of sh4.exe functions, by address: tools/regen.sh passes this file to the
 * recompiler (--exclude-manual), which then leaves these functions out of src/recomp/gen. A function declared
 * `extern void sub_X_gen(void);` here is still generated, as sub_X_gen, for the replacement to call. */
#include "../runtime/rt.h"
#include "../runtime/host.h"
#include <math.h>

/* The static CRT's memcpy (memmove semantics) and its second copy (0x57FE90, 0x576BC0): their jump tables sit
 * past the end the disassembler finds and are indexed backwards, so the lifted copies cannot follow them. Both
 * are cdecl with an ebp frame: the bridge just pops the return address. */
static void crt_memmove(void) {
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2);
    memmove((void *)XBOX_PTR(d), (const void *)XBOX_PTR(s), n);
    g_eax = d;
    g_esp += 4;  /* ret: cdecl */
}
void sub_0057FE90(void) { crt_memmove(); }
void sub_00576BC0(void) { crt_memmove(); }

/* ---- widescreen (ThirteenAG's SilentHill4.WidescreenFix, ported) ----
 * The game draws at the screen's resolution (resolution= in sh4.ini, 1280x720 by default), whatever its own
 * Resolution option says. On a screen wider than 4:3 the camera widens (hor+), the 2D keeps a centered 4:3 area,
 * full-screen fades and filters stretch across the screen, and the cutscene borders and a few screen-space
 * effects drawn for 4:3 are left out. At 4:3 every value below is the game's own.
 * Most of the fix acts on calls, told apart by their return address; tools/patch_exe.py points the few constants
 * inside functions at the variables at VARS, or turns them into calls to the cave (sub_005A9379). */
extern uint32_t rt_res_w, rt_res_h;
#define VARS 0x0EC00000u
#define V_MENU_SCALE (VARS + 0x0)  /* fmul at 0x404DAC, 1/256 */
#define V_MAP_ZOOM (VARS + 0x4)    /* fmul at 0x51D65B, 0.5 */
#define V_FILM_WIDTH (VARS + 0x8)  /* fmuls at 0x5681AF..0x568337 (loading animation, the bathroom hole), 0.1 */
#define V_BLUR_640 (VARS + 0xC)    /* mov eax at 0x566BA9, 640 */
static int ws;                 /* the screen is wider than 4:3 */
static float hud_scale = 1.0f; /* a 4:3 area's share of the screen's width */
static float hud_offset;       /* the extra width on each side, in the 2D's 640x480 units */

static void ws_init(void) {
    float aspect = (float)rt_res_w / rt_res_h;
    ws = aspect > 4.0f / 3.0f + 0.01f;
    if (!ws) aspect = 4.0f / 3.0f;
    hud_scale = (4.0f / 3.0f) / aspect;
    hud_offset = (480.0f * aspect - 640.0f) / 2.0f;
    host_arena_commit(VARS, 0x1000);
    MEMF(V_MENU_SCALE) = 0.00390625f * hud_scale;
    MEMF(V_MAP_ZOOM) = 0.5f / hud_scale;
    MEMF(V_FILM_WIDTH) = 0.1f * hud_scale;
    MEM32(V_BLUR_640) = (uint32_t)(int32_t)lroundf((640.0f - 640.0f / hud_scale) * 8.0f + 640.0f / hud_scale);
    rt_log("widescreen: %ux%u, %s (2D scale %.4f, offset %.2f)", rt_res_w, rt_res_h, ws ? "on" : "off (4:3)",
           hud_scale, hud_offset);
}

/* The display mode: the device is created and reset at the screen's resolution, and the Resolution option's
 * presets (0x4148E0: 640x480 up to 3840x2160 in GOG's build) all give it. */
#define RES_W 0x5D524Cu
#define RES_H 0x5D5250u
static void force_mode(void) {
    MEM32(RES_W) = rt_res_w;
    MEM32(RES_H) = rt_res_h;
}
extern void sub_00402F20_gen(void);
void sub_00402F20(void) { force_mode(); sub_00402F20_gen(); }  /* creates the device */
extern void sub_00414150_gen(void);
void sub_00414150(void) { sub_00414150_gen(); force_mode(); }  /* the default options: 1024x768 */
extern void sub_00402CD0_gen(void);
void sub_00402CD0(void) { force_mode(); sub_00402CD0_gen(); }  /* resets it */
extern void sub_004148E0_gen(void);
void sub_004148E0(void) { sub_004148E0_gen(); force_mode(); }  /* applies the Resolution option */

/* The camera's projection: its vertical scale (+0x48, 0.78125 at 4:3) and horizontal one (+0x44, 1.0), set when a
 * camera is made (0x41D150, which starts +0x48 at 7/6) and by the room camera's setup (calls from 0x55BDAC and
 * 0x55BDBB, the second through GOG's code at 0x5A9360). Hor+: both shrink by the 4:3 area's share. */
extern void sub_0041D150_gen(void);
void sub_0041D150(void) {
    uint32_t cam = ARG(0);
    sub_0041D150_gen();
    if (ws) MEMF(cam + 0x48) = 0.78125f * hud_scale;
}
extern void sub_0041CA50_gen(void);
void sub_0041CA50(void) {
    if (ws && MEM32(g_esp) == 0x55BDB1u) ARGF(1) = 0.78125f * hud_scale;
    sub_0041CA50_gen();
}
extern void sub_0041CA70_gen(void);
void sub_0041CA70(void) {
    if (ws && MEM32(g_esp) == 0x55BDC0u) ARGF(1) = hud_scale;
    sub_0041CA70_gen();
}

/* The text's horizontal scale and left edge, built on the stack by 0x404D80 (`mov [esp+24], 0.003125` at
 * 0x404DED and `mov [esp+34], -1.0` at 0x404E17, both calls to this cave now). */
void sub_005A9379(void) {
    uint32_t from = MEM32(g_esp);
    g_esp += 4;  /* ret */
    if (from == 0x404DF2u) MEMF(g_esp + 0x24) = 0.003125f * hud_scale;
    else if (from == 0x404E1Cu) MEMF(g_esp + 0x34) = -1.0f * hud_scale;
    else rt_fatal("widescreen cave called from %08X", from);
}

/* The in-game overlay (0x433C70) asks for its size: 512 wide (0x55A0C0) and 448 high (0x55A0D0). It keeps a 4:3
 * width, and the x it turns into a float right before asking for the height is scaled to match. */
extern void sub_0055A0C0_gen(void);
void sub_0055A0C0(void) {
    uint32_t from = MEM32(g_esp);
    if (ws && (from == 0x433D72u || from == 0x433DF4u)) {
        g_eax = (uint32_t)(int32_t)(512.0f * hud_scale);
        g_esp += 4;
        return;
    }
    sub_0055A0C0_gen();
}
extern void sub_0055A0D0_gen(void);
void sub_0055A0D0(void) {
    uint32_t from = MEM32(g_esp);
    if (ws && from == 0x433D96u) MEMF(g_esp + 4 + 0x20) /= hud_scale;  /* fild/fstp [esp+20] at 0x433D89 */
    if (ws && from == 0x433E18u) MEMF(g_esp + 4 + 0x38) /= hud_scale;  /* fild [esp+50], fstp [esp+38] at 0x433E0B */
    sub_0055A0D0_gen();
}

/* Screen-space quads drawn for 4:3 that a wide screen leaves out: the cutscene wall darkness and grunge (calls
 * from 0x42E839, 0x45EBD2), the blood stains (0x477717) and one more wall effect (0x42EAC0, the whole function). */
extern void sub_00564210_gen(void);
void sub_00564210(void) {
    uint32_t from = MEM32(g_esp);
    if (ws && (from == 0x42E83Eu || from == 0x45EBD7u || from == 0x47771Cu)) {
        g_esp += 4;  /* cdecl: the caller pops the arguments */
        return;
    }
    sub_00564210_gen();
}
extern void sub_0042EAC0_gen(void);
void sub_0042EAC0(void) {
    if (ws) { g_esp += 4; return; }
    sub_0042EAC0_gen();
}

/* Cutscene borders (0x4F49AA, 0x4F49C2 draw them when their flags are set): none on a wide screen. */
extern void sub_00565840_gen(void);
void sub_00565840(void) {
    uint32_t from = MEM32(g_esp);
    if (ws && (from == 0x4F49BFu || from == 0x4F49D7u)) {
        g_eax = 0;
        g_esp += 4;
        return;
    }
    sub_00565840_gen();
}

/* Every 2D quad goes through 0x563D20(x, y, w, h, u0, v0, u1, v1, color, color), in 640x480 units. Its 71 calls,
 * by return address (the fix numbers them in address order; that order is the same in GOG's exe): the intro and
 * depth-of-field overlays move left and widen by the extra width, the blur ones only move left, four (menu and
 * save menu backgrounds) stay as they are, the movie widens both ways, and any other call that covers the whole
 * 640x480 screen (or the 512x448 in-game overlay) is stretched over the wide one. */
enum { OV_FULL, OV_INTRO_DOF, OV_BLUR, OV_KEEP, OV_MOVIE };
static const struct { uint32_t from; uint8_t kind; } ov_calls[] = {
    {0x412E14u, OV_MOVIE},
    {0x55311Fu, OV_KEEP}, {0x56821Fu, OV_KEEP}, {0x56FE2Fu, OV_KEEP}, {0x570555u, OV_KEEP},
    {0x565DD2u, OV_INTRO_DOF}, {0x565E37u, OV_INTRO_DOF}, {0x565E68u, OV_INTRO_DOF}, {0x565E96u, OV_INTRO_DOF},
    {0x565EC7u, OV_INTRO_DOF}, {0x565F30u, OV_INTRO_DOF}, {0x566621u, OV_INTRO_DOF}, {0x566691u, OV_INTRO_DOF},
    {0x566721u, OV_INTRO_DOF}, {0x566791u, OV_INTRO_DOF}, {0x566825u, OV_INTRO_DOF}, {0x566895u, OV_INTRO_DOF},
    {0x566926u, OV_INTRO_DOF}, {0x566996u, OV_INTRO_DOF}, {0x566C5Fu, OV_INTRO_DOF},
    {0x566BCDu, OV_BLUR}, {0x568333u, OV_BLUR}, {0x56FB42u, OV_BLUR}, {0x56FD63u, OV_BLUR},
};
extern void sub_00563D20_gen(void);
void sub_00563D20(void) {
    if (ws) {
        uint32_t from = MEM32(g_esp);
        int kind = OV_FULL;
        for (size_t i = 0; i < sizeof ov_calls / sizeof *ov_calls; i++)
            if (ov_calls[i].from == from) { kind = ov_calls[i].kind; break; }
        float x = ARGF(0), y = ARGF(1), w = ARGF(2), h = ARGF(3);
        switch (kind) {
        case OV_FULL:
            if ((x >= -2.0f && x <= 2.0f && y >= -2.0f && y <= 2.0f && w >= 638.0f && w <= 642.0f && h >= 478.0f &&
                 h <= 482.0f) || (w == 512.0f && h == 448.0f)) {
                ARGF(0) = x - hud_offset;
                ARGF(2) = w + 2.0f * hud_offset;
            }
            break;
        case OV_INTRO_DOF: ARGF(0) = x - hud_offset; ARGF(2) = w + hud_offset; break;
        case OV_BLUR: ARGF(0) = x - hud_offset; break;
        case OV_MOVIE:
            ARGF(0) = x - hud_offset; ARGF(2) = w + hud_offset;
            ARGF(1) = y - hud_offset * 0.75f; ARGF(3) = h + hud_offset * 0.75f;
            break;
        }
    }
    sub_00563D20_gen();
}

/* Called once after sh4.exe is loaded. */
void game_init(void) {
    ws_init();
    force_mode();
}

/* Indirect calls and the dispatch table reach the generated bodies (sub_X_gen) of wrapped functions: calls
 * by address are routed here first. */
recomp_func_t recomp_lookup_manual(uint32_t va) {
    switch (va) {
    case 0x0057FE90u: return sub_0057FE90;
    case 0x00576BC0u: return sub_00576BC0;
    case 0x00402F20u: return sub_00402F20;
    case 0x00414150u: return sub_00414150;
    case 0x00402CD0u: return sub_00402CD0;
    case 0x004148E0u: return sub_004148E0;
    case 0x0041D150u: return sub_0041D150;
    case 0x0041CA50u: return sub_0041CA50;
    case 0x0041CA70u: return sub_0041CA70;
    case 0x005A9379u: return sub_005A9379;
    case 0x0055A0C0u: return sub_0055A0C0;
    case 0x0055A0D0u: return sub_0055A0D0;
    case 0x00564210u: return sub_00564210;
    case 0x0042EAC0u: return sub_0042EAC0;
    case 0x00565840u: return sub_00565840;
    case 0x00563D20u: return sub_00563D20;
    }
    return NULL;
}

/* ScopeWalker - platform.h
 *
 * The OS-specific seam. Implemented once per platform:
 *   platform_win.c  (Win32)      - exists today, to be split out of scopewalker.c
 *   platform_mac.m  (AppKit)     - to write
 *   platform_x11.c  (X11)        - to write
 *
 * The portable core and the SDL3 shell depend ONLY on this header, never on
 * windows.h / Cocoa / X11 directly. See PORTING.md for the full plan.
 */
#ifndef SCOPEWALKER_PLATFORM_H
#define SCOPEWALKER_PLATFORM_H

#include <stdbool.h>
#include "core/draw.h"   /* Pixel */

/* Abstract key ids, mapped to VK_* / kVK_* / X keysyms by each backend. */
typedef enum {
    PK_MOD_ALT = 0, PK_MOD_CTRL, PK_MOD_SHIFT, PK_MOD_SUPER,
    PK_PAUSE_ESC, PK_PAUSE_F2, PK_PAUSE_F3
} PlatKey;

/* --- Screen capture ---------------------------------------------------------
 * Copy the screen rectangle (x,y,w,h) into `out` (w*h pixels, 0x00RRGGBB,
 * top-down). `out` is caller-owned and large enough. Returns false on failure.
 */
bool plat_capture(int x,int y,int w,int h,Pixel *out);

/* Single pixel under the cursor (for the live "RGB at cursor" readout). */
bool plat_pixel_at(int x,int y,unsigned char *r,unsigned char *g,unsigned char *b);

/* --- Global input (valid even when our window is not focused) --------------- */
bool plat_key_down(PlatKey key);
bool plat_mouse_left_down(void);
void plat_cursor_pos(int *x,int *y);

/* --- Native window tweaks (applied to the SDL window's native handle) ------- */
/* Exclude a window from screen capture, so the selection frame stays out of
   the scopes. No-op where unsupported. */
void plat_exclude_from_capture(void *native_window);
/* Follow the OS light/dark setting for the title bar, where applicable. */
void plat_apply_dark_titlebar(void *native_window);

#endif /* SCOPEWALKER_PLATFORM_H */

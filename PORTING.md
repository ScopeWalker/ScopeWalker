# ScopeWalker — Cross-platform porting plan

Work happens on the `cross-platform` branch. `main` stays a working Windows build.

## Architecture decision

Chosen approach: **shared portable core + one common SDL3 shell + thin native
per-OS modules** for the three OS-specific parts.

```
              core/           (portable C, no OS headers)  💎
        ┌───────┴────────┐    scope math + AA rendering
        │  SDL3 shell     │    window, present pixel buffer, in-window mouse
        └───────┬────────┘
     ┌──────────┼───────────┐
 platform_win  platform_mac  platform_x11   (thin native modules)
   (Win32)      (AppKit /      (X11 /
                ScreenCaptureKit) Wayland later)
```

Why: ScopeWalker's UI is already **custom-drawn into a pixel buffer** (see
`RadioAA` / `CheckAA`), not native widgets — so SDL3 carries the whole UI across
OSes for free. The genuinely OS-specific parts (screen capture, global input,
overlay-excluded-from-capture) require native code under *any* approach, so they
are isolated behind `platform.h`.

## The platform seam (`src/platform.h`)

Only three things are truly OS-specific. Every OS module implements them:

1. **Screen capture** — copy a screen rectangle into a `Pixel` buffer.
   - Windows: `BitBlt` + `CAPTUREBLT` (already written in `scopewalker.c`)
   - macOS: ScreenCaptureKit (needs *Screen Recording* permission)
   - Linux: X11 `XGetImage` first; Wayland via `xdg-desktop-portal` + PipeWire later
2. **Global input** — modifier key + mouse state while our window is NOT focused
   (the Alt+drag selection works on top of any app).
   - Windows: `GetAsyncKeyState` / `GetCursorPos`
   - macOS: `CGEventSource` (needs *Accessibility* permission)
   - Linux: X11 `XQueryPointer` / `XQueryKeymap`
3. **Overlay excluded from capture** — the selection frame must not appear in the
   scopes.
   - Windows: `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`
   - macOS: `NSWindow.sharingType = .none`
   - Linux: no universal API — fall back to "capture, then don't draw over region"

SDL3 exposes each window's native handle (`SDL_GetWindowProperties`), so native
attributes (capture exclusion, dark title bar) can be applied to SDL windows.

## Phases

- [x] **Phase 1 — extract portable draw core** → `src/core/draw.{h,c}`.
      Windows behaviour unchanged, still builds with MSVC. *(done)*
- [x] **Phase 1b — extract the render hot path**: `RenderVec/RenderWaveLike/
      RenderHist` moved to `src/core/scopes.{h,c}`, driven by `CoreScope`/
      `CoreParams` instead of globals. No text involved; Windows behaviour
      unchanged. *(done)*
- [x] **Phase 1c — extract the graticules** (`Tpl*`): moved to `core/scopes.c` as
      `CoreTplVec/CoreTplWaveLike/CoreTplHist`. Text is delegated to a `CoreTextFn`
      callback supplied by the shell (GDI on Windows, so labels stay pixel-perfect;
      native/bitmap on other OSes later). The shell owns the `bits`→`tpl` snapshot
      (with `GdiFlush()` first on Windows). **The portable core is now complete** —
      all scope math, rendering and graticule geometry live in `src/core/`, with no
      OS dependency. Verified on Windows (0 warnings, screenshot-checked).
- [ ] **Phase 2 — SDL3 shell on Windows**: replace window/present/in-window input
      with SDL3; keep Win32 capture. Proves the architecture end-to-end.
- [ ] **Phase 3 — macOS module**: ScreenCaptureKit capture + CGEventSource input
      + NSWindow overlay. Then codesign/notarize for distribution.
- [ ] **Phase 4 — Linux module (X11)**: XGetImage capture + XQueryPointer input.
- [ ] **Phase 5 — multi-OS CI**: GitHub Actions builds windows/macos/ubuntu and
      attaches all three binaries to each release.

## Dependencies to add (Phase 2)

- **SDL3** (zlib license). Vendored or fetched in CI. Adds ~1–2 MB to the binary.
- A small embedded bitmap font for Phase 1b (e.g. a public-domain 8×N font), to
  replace GDI `TextOutA` in the core.

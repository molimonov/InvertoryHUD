# Rendering and Integration Research

Status: **P2/P5 complete for the tested build.**

## 1. Graphics API

The game imports:

- `d3d9.dll`
- `d3dx9_29.dll`

The rendering device is created from `S2CE::CGraphicDevice` (`GraphicDevice.cpp`
strings in the decompilation). Relevant globals/fields:

| Address / offset | Meaning |
|---|---|
| `0x006D0854` | `CGraphicDevice*` global (`DAT_006d0854`) |
| `CGraphicDevice + 0x0AC` | `IDirect3D9*` |
| `CGraphicDevice + 0x0B0` | `IDirect3DDevice9*` |
| `CGraphicDevice + 0x060` | graphics-ready byte |

`Direct3DCreate9(0x20)` is called in `FUN_00517A40` (constructor), and the
device is created later by the device code (`CreateDevice` log strings).

Runtime check with `Probe.exe`:

```text
CGraphicDevice global 0x006D0854 = ...
  +0x60 ready=1  +0xAC d3d9=...  +0xB0 device=...
  vtable=...  Reset=0x6EBD6500  Present=0x6EBD6120
```

`Reset`/`Present` are the standard `d3d9.dll` entries.

## 2. Hook choice

The game calls `IDirect3DDevice9::Present` through the device vtable. The DLL:

1. Reads `CGraphicDevice` from `0x006D0854`.
2. Reads the device at `+0xB0`.
3. Patches the shared device vtable:
   - index `17` (`0x44`) = `Present`;
   - index `16` (`0x40`) = `Reset`.

No code bytes are patched and no MinHook trampoline is needed. The original
function pointers are saved and called after the HUD has drawn.

The vtable region is writable in the tested runtime, but the patch still uses
`VirtualProtect` to avoid assuming page protection.

### Why Present

- It is called after the game has finished the frame (after `EndScene`), so the
  HUD appears over world and UI.
- It is called once per frame.
- It survives resolution/device changes better than hooking an internal UI
  function.
- `Reset` is hooked only to release/recreate HUD resources before/after a
  device reset.

## 3. HUD rendering

- Fixed-function pipeline, no custom shaders.
- Vertex format: `D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1`.
- Quads are rendered with `DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, ...)`.
  > `D3DPT_TRIANGLELIST` with 4 vertices was the source of a large stray
  > triangle: the list mode expects 6 vertices and read stack garbage.
- A reusable `IDirect3DStateBlock9` captures the game state before HUD drawing
  and restores it afterwards.
- The HUD resets: vertex/pixel shaders, vertex declaration, stream source,
  indices, FVF, z-test/write, culling, lighting, fog, clip planes, stencil,
  vertex blending, extra texture stages, sampler states and color write mask.
- Text and icons use only the HUD's own textures; the game's textures/states are
  restored by the state block.

### Icons

`data/gfx/uiElements1.dds` is encrypted with the game's `rc00` container.
The DLL decrypts it in memory (DNG/AdK-compatible LZSS + XOR + per-name key) and
loads it with `D3DXCreateTextureFromFileInMemory`. The atlas is 512×512 DXT5
with 48×48 cells. No game file is modified.

### Text

`ID3DXFont` was rejected:

- The D3DX9 font interface vtable differs from the minimal declaration by two
  methods (`GetDC`, `PreloadCharacters`).
- Calling `DrawTextA` from the Present hook caused stack/state corruption and a
  game crash.

The shipped renderer builds its own ASCII font atlas once with GDI:

- `CreateDIBSection` (32-bit top-down), `Tahoma` (fallback `Segoe UI`, `Arial`);
- glyphs 32..126, tight ink rectangles and advances;
- alpha = max(R,G,B), RGB forced to white so the vertex color tints the text;
- uploaded once into an `A8R8G8B8` managed texture;
- text is drawn with the same quad routine as icons.

This removes D3DXFont from the hook path entirely.

## 4. Device loss / reset

- `Present`/`Reset` hooks are installed on the shared device vtable.
- On `Reset`, `hud::OnDeviceLost()` releases the font atlas, icon atlas, white
  texture and state block; the original `Reset` is called; resources are
  recreated lazily on the next `Present`.
- If the device pointer changes (windowed/fullscreen or resolution changes), the
  hook installer checks the vtable and re-patches it.

## 5. Input

The game creates a helper `D3DProxyWindow` that is **not** the window receiving
mouse input. Choosing it as the HUD window caused clicks to pass through to the
game world and made cursor-position diagnostics report
`not foreground`.

The DLL:

1. Enumerates top-level windows of the current process.
2. Skips `D3DProxyWindow`.
3. Prefers the real game window (foreground window with the process id).
4. Subclasses that window's `WndProc` and re-checks every 2 seconds.
5. Installs a `WH_MOUSE_LL` hook, but only over the real game window:
   - `WM_LBUTTONDOWN/UP`, `WM_RBUTTONDOWN/UP`, `WM_MOUSEWHEEL` over the HUD are
     swallowed and a pending click is queued for the render thread;
   - mouse movement is never swallowed, so the game cursor can move freely over
     the panel;
   - clicks outside the HUD pass through unchanged.

The HUD geometry is published to the hook in viewport coordinates; the hook maps
screen coordinates through the real game client rect.

## 6. Window/UI notes

- The game's UI is hardcoded C++ (no XML layout in the tested build); there is
  no external UI layout file to extend.
- The original Inventory screen is the reference for the data, not for the UI.
- The HUD draws above the game UI in `Present`; it is intentionally not part of
  the game's own UI hierarchy.

## 7. Performance

- Inventory sampling: every 500 ms.
- Icon atlas and font atlas: created once per device/resolution; textures are
  cached.
- Text is drawn as a small number of textured quads; no per-frame font texture
  creation.
- No per-frame heap allocations are required for the steady state (the settings
  row vector reuses capacity).
- Logging is rate-limited; the release build does not log every frame.

## 8. Confidence

| Finding | Confidence |
|---|---|
| Game uses Direct3D 9 | VERIFIED (imports + runtime) |
| `CGraphicDevice` / device offsets | VERIFIED (disassembly + runtime) |
| `Present`/`Reset` vtable indices | VERIFIED (runtime) |
| Vtable patch is sufficient | VERIFIED (HUD renders in game) |
| D3DXFont interface is unsafe here | VERIFIED (crash + fix) |
| GDI font atlas path | VERIFIED (HUD text renders) |
| Real game window vs proxy window | VERIFIED (runtime logs) |
| Low-level hook blocks only HUD clicks | implemented; test on target build |

# InvertoryHUD

A lightweight in-game resource HUD for **The Settlers II: 10th Anniversary / Юбилейное издание** (the `s2dng_addon.exe` edition).

The HUD reads the **original global inventory** used by the game's own
`Statistics → Inventory` screen (`pl_getGoodAmount` / `CPlayer::goods`), and draws
a compact icon panel with live amounts directly in the DirectX 9 frame.

> This is an unofficial fan mod. It does not modify the original EXE, original
> DLLs or game data files.

## Features

- Live global inventory for the current player (town hall + all warehouses).
- Compact icon panel: original game icons, amounts and optional net change.
- Panel placement: top-center by default; configurable anchors and offsets.
- Settings window (gear button) with checkboxes for all 33 goods.
- Settings are stored in `InvertoryHUD.ini`.
- Automatic DLL injection through the `S2_HUD.exe` launcher.
- Single-file installer that asks for the game folder and can create a desktop shortcut.

## Requirements

- Windows 10/11 (x86 game process).
- **The Settlers II: 10th Anniversary / Юбилейное издание** with
  `bin\s2dng_addon.exe`.
- DirectX 9 runtime (`d3d9.dll`, `d3dx9_29.dll`), already required by the game.

The current build is verified against:

| Property | Value |
|---|---|
| Executable | `s2dng_addon.exe` |
| Size | `3,252,224` bytes |
| SHA-256 | `f461cbaa27e523a0ae8eb6fd6e318c5c21df5e2eddda9f6a4d424aa08b8c17e8` |
| Image base | `0x00400000` |
| TimeDateStamp | `0x45BF9600` |
| SizeOfImage | `0x0032E000` |

Other builds are rejected by the compatibility check instead of risking a crash.

## Install

1. Download `InvertoryHUD_Setup.exe` from the latest release.
2. Run it and select the game folder, for example:
   `D:\Games\The Settlers. Зарождение цивилизаций`.
3. The installer finds `bin\s2dng_addon.exe` and copies:
   - `InvertoryHUD.dll`
   - `S2_HUD.exe`
   - `InvertoryHUD.ini` (only if it does not exist yet)
4. Launch the game through `S2_HUD.exe` (or the desktop shortcut created by the installer).
   The launcher starts `s2dng_addon.exe` and injects the DLL automatically.

The original `s2dng_addon.exe` shortcut still works, but it does not load the HUD.

### Manual injection

For development, start the game normally and run:

```text
bin\Injector.exe
```

`Probe.exe` prints game/device/player/inventory diagnostics and is only a
research tool.

## Panel and settings

- The panel is shown only while a match is loaded and a local player is active.
- Click the gear at the right end of the panel to open settings.
- Each checkbox adds/removes a resource immediately and saves the INI.
- `Changes` toggles the `+N/-N` net-change display.
- `All` / `None` selects or clears every resource.
- `X` closes the settings window.

### `InvertoryHUD.ini`

```ini
[General]
Enabled=1
ShowChanges=1

[Position]
AnchorCenter=1
AnchorRight=0
AnchorBottom=0
X=0
Y=12

[Appearance]
IconSize=18
FontSize=11
BackgroundAlpha=170

[Resources]
Visible=wood,plank,stone,corn,bread
```

`X`/`Y` are offsets in 1024×768 reference pixels and are scaled to the current
viewport. `AnchorCenter=1` centers the panel horizontally; `AnchorRight` and
`AnchorBottom` select other edges. Press `F10` to reload the INI in-game.

## Build

Requirements: CMake 3.20+, Visual Studio 2022/2026 with the x86 C++ toolchain,
Windows SDK, Python 3 (for the installer payload generator).

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release --parallel
```

Outputs in `build/Release`:

- `InvertoryHUD.dll` — the mod.
- `S2_HUD.exe` — the automatic launcher.
- `Injector.exe` — manual injector.
- `Probe.exe` — diagnostic tool.
- `InvertoryHUD_Setup.exe` — self-contained installer (also copied to `dist/`).

## Architecture

```text
S2_HUD.exe
  └─ starts s2dng_addon.exe and injects InvertoryHUD.dll

InvertoryHUD.dll
  ├─ GameBridge      — version check, game manager/player lookup
  ├─ InventoryReader — read-only traversal of CPlayer::goods (std::map)
  ├─ D3D9Hook        — IDirect3DDevice9::Present/Reset vtable hooks
  ├─ HUDRenderer     — panel, icons, text, settings window
  ├─ FontAtlas       — GDI-generated ASCII atlas, no D3DXFont dependency
  ├─ MouseHook       — swallows clicks over the HUD only
  └─ Config/Logger   — INI and log file
```

### Research notes

The reverse-engineering details, verified addresses and structure layouts are in:

- [`docs/INVENTORY_RESEARCH.md`](docs/INVENTORY_RESEARCH.md)
- [`docs/RENDERING_RESEARCH.md`](docs/RENDERING_RESEARCH.md)
- [`docs/RESEARCH_REPORT.md`](docs/RESEARCH_REPORT.md)
- [`docs/BUILD.md`](docs/BUILD.md)

## Known limitations

- Only the `s2dng_addon.exe` build listed above is supported.
- The HUD shows the **depot/castle** global stock, matching the game's
  `Statistics → Inventory` screen. Goods in transit or in production are not
  separately reported.
- `+N/-N` is the net change since the previous 500 ms sample, not a verified
  production/consumption statistic.
- The installer does not uninstall automatically; delete the copied files to
  remove the mod.
- The launcher icon and the in-game goods icons come from the user's game
  installation. They remain the property of their respective owners.

## License

The source code of this project is released under the MIT License. See
[`LICENSE`](LICENSE). Game assets, the extracted game icon, original DLLs and
game data are **not** licensed by this project and remain the property of their
respective owners.

# Research Report — InvertoryHUD

This report answers the questions from `Agent.md` (section 17) for the tested
`s2dng_addon.exe` build.

## Where are global resources stored?

Per player, in `CPlayer::goods` at `CPlayer + 0x114`. It is a
`std::map<int,int>` keyed by the goods id from `Goods.lua`. The player object is
500 bytes; the player array pointer is at `*(void**)(0x006CAC60 + 0x14)`, and
player `i` is at `players + 4 + i*500`.

## Which function does the original Statistics screen use?

The Inventory screen reads the same map through `FUN_00461FB0`:

```text
statistics object + 0x688 → game object
game object + 0x6118     → current CPlayer*
CPlayer + 0x114          → goods map
```

`FUN_0049B710` / `FUN_0049B880` update the goods/settler labels. The Lua binding
`logic.pl_getGoodAmount` (`FUN_0044CD60`) also resolves to the same map.

## Can data be obtained without opening the Statistics window?

Yes. The HUD reads `CPlayer::goods` directly from the render thread every 500 ms.
It does not open or depend on the Statistics window. This was verified in a
running match with `Probe.exe`.

## How is the current player determined?

Scan the 8-player array for the first player with `active` (`+0x164`) and
`type == 2` (`+0x0C`). This matches `pl_getUser` (`0x0044C990`). If no local
human is active, the HUD is hidden.

## Which resource types exist?

33 non-group goods are exposed by the tested `Goods.lua`. Their ids and icon
coordinates are listed in
[`INVENTORY_RESEARCH.md`](INVENTORY_RESEARCH.md#6-goods-enum). Group entries
(`food`, `tools`, `weapons`) are not stock entries.

## Where are the original goods icons?

`data/gfx/uiElements1.dds` (encrypted `rc00` container), decrypted at runtime.
512×512 DXT5, 10×5 grid of 48×48 cells. The cell coordinates come from
`properties.gp_setIconXY` in `Goods.lua`.

## Which DirectX hook is used?

Vtable hooks on `IDirect3DDevice9`:

- index 17 — `Present`;
- index 16 — `Reset` (resource release/recreate).

The device is obtained from `CGraphicDevice` (`0x006D0854`) + `0xB0`.
`Present` was chosen because it runs after the game's frame and survives
resolution/device changes. `EndScene`/internal UI hooks were not needed.

## Can the original font be used?

Not directly. The original UI uses a custom font/atlas; `ID3DXFont` from
`d3dx9_29.dll` crashed when called from the Present hook due to interface/vtable
differences. The shipped HUD builds its own small GDI atlas and draws text with
its own quads. The original game font can be investigated later.

## How is the HUD integrated safely?

- No game code bytes are modified.
- Only the shared device vtable is patched; original pointers are saved.
- A state block restores all device state after HUD drawing.
- Resources are released before `Reset` and recreated lazily.
- A compatibility check verifies the game build before installing hooks.
- Inventory reads are read-only and wrapped in safe readers.

## How are settings with checkboxes implemented?

- The gear button opens an in-HUD settings panel.
- Checkboxes for all 33 goods toggle `visible_resources` immediately.
- `Changes`, `All`, `None`, and close controls are provided.
- `InvertoryHUD.ini` stores the result; `F10` reloads it.
- The settings window is horizontally centered and does not move when the
  resource panel changes width.

## How can production/consumption be obtained?

Not yet verified. The current `+N/-N` is the net change between two 500 ms
samples, explicitly labelled as a change, not as production/consumption. The
next research step is to find the original `WareStatistics`/production counters
or the goods-transfer call sites.

## Technical limitations

- Only the tested `s2dng_addon.exe` build is supported.
- The base `s2dng.exe` has different addresses and is not supported.
- The map reports depot/castle stock; goods in transit/production are not
  separately exposed.
- The game must be launched through `S2_HUD.exe` for automatic injection, or
  `Injector.exe` must be run manually.
- The installer has no uninstaller; removing the copied files is enough.
- The game icon is extracted from the user's installation and remains the
  property of its owners.

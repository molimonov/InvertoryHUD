# Inventory Research — `s2dng_addon.exe`

Status: **P1 complete for the tested build.**

This document records the verified source of the game's global inventory and the
data structures used by `InvertoryHUD.dll`. It is based on:

1. Ghidra decompilation of `s2dng_addon.exe` (`s2dng_addon.exe.c`).
2. Direct disassembly of the same binary.
3. Runtime inspection with `Probe.exe` (process memory, no code execution).
4. Comparison with the original **Statistics → Inventory** screen in a running
   game.

## 1. Game build

| Property | Value |
|---|---|
| File | `bin/s2dng_addon.exe` |
| Size | `3,252,224` bytes |
| SHA-256 | `f461cbaa27e523a0ae8eb6fd6e318c5c21df5e2eddda9f6a4d424aa08b8c17e8` |
| Machine | `IMAGE_FILE_MACHINE_I386` (x86) |
| Image base | `0x00400000` |
| TimeDateStamp | `0x45BF9600` |
| SizeOfImage | `0x0032E000` |
| Relocations | none (`DllCharacteristics = 0`, no `.reloc`) |

The DLL checks `SizeOfImage`, `TimeDateStamp` and the executable name before
installing hooks. Addresses below are absolute virtual addresses for this
build; the source stores them as RVAs from `0x00400000`.

## 2. Authoritative Lua entry point

The game registers the scripting API function:

```text
logic.pl_getGoodAmount(playerId, goodType) -> amount
```

Registration table: `0x0044CD60` (`FUN_0044CD60`), entry `"pl_getGoodAmount"`.

Verified implementation flow (disassembly at `0x0044CD60`):

```text
manager = *(void**)0x006CAC60                 ; game/logic manager
playerId = Lua arg 1
if (playerId < 0 || playerId >= 8) return
players = *(void**)(manager + 0x14)           ; CPlayer array
player = players + 4 + playerId * 500         ; FUN_00456A10
if (*(uint8_t*)(player + 0x164) == 0) return  ; active flag
goodId = Lua arg 2
amount = std::map_find(player + 0x114, goodId)
push amount to Lua
```

### `FUN_00456A10`

```c
int __thiscall get_player(int players, int* player_id) {
    return *player_id * 500 + 4 + players;
}
```

Therefore:

- `CPlayer` stride: **500 (0x1F4)** bytes.
- Player 0 address: `players + 4`.
- `players` array pointer: `*(void**)(manager + 0x14)`.
- Manager global: `0x006CAC60` (`DAT_006cac60`).

### `FUN_00454870`

```c
return *(uint8_t*)(player + 0x164);
```

The active/in-game flag is at `CPlayer + 0x164`.

### `FUN_0045D880`

Returns `DAT_006cac60`. This is the manager used by all `pl_*` Lua bindings.

## 3. `CPlayer` layout (verified fields)

`PLAYER_BASE = *(void**)(0x006CAC60 + 0x14) + 4 + index * 500`

| Offset | Type | Meaning | Confidence |
|---:|---|---|---|
| `+0x000` | ptr | vtable/object header | VERIFIED |
| `+0x004` | int | player index (0..7) | VERIFIED |
| `+0x00C` | int | player type; `2` = local human | VERIFIED |
| `+0x114` | `std::map<int,int>` | global goods: good id → amount | VERIFIED |
| `+0x164` | uint8 | active flag | VERIFIED |

The local user is found by scanning players for:

```c
active && type == 2
```

This is exactly what `pl_getUser` (`0x0044C990` → `thunk_FUN_00703CCC` →
`FUN_00456A60`) does.

## 4. The goods map

`FUN_00461FB0` is a `std::map<int,int>` lookup:

```c
int get_goods(std::map<int,int>* map, int good_id)
```

Disassembly at `0x00461FB0`:

```text
mov eax, [esp+4]          ; key
mov esi, ecx              ; map owner
lea ecx, [esi+8]          ; std::map object
call std::map::find       ; FUN_00585130
mov ecx, [esi+0xC]        ; end node
...
mov eax, [eax+0x10]       ; value
```

Tree layout, derived from `FUN_00585130`:

| Offset | Meaning |
|---:|---|
| `owner + 0x008` | `std::map` object |
| `owner + 0x00C` | map header / end node |
| `header + 0x004` | root node |
| `node + 0x000` | left child |
| `node + 0x004` | parent |
| `node + 0x008` | right child |
| `node + 0x00C` | int key (goods id) |
| `node + 0x010` | int value (amount) |
| `node + 0x015` | `is_nil` byte |

The map key is the same 32-bit id returned by `properties.gp_create("...")` in
`data/scripts/properties/Goods.lua`.

> The DLL attempts the native `FUN_00461FB0` call first, but calling it from the
> Present hook produced an SEH fault inside the map lookup. The shipped reader
> performs a read-only traversal of the same tree; it does not modify the map
> and does not duplicate the economic model.

## 5. Original Statistics → Inventory screen

The screen is opened by `FUN_0049C7D0` (`0x0049C7D0`, `STATS_MENUOPENED =
"Inventory"`). It calls:

- `FUN_0049BB00` — fills the goods-id array at `stats + 0x8398`;
- `FUN_0049B710` — updates the amount labels.

`FUN_0049B710` at `0x0049B710`:

```text
eax = *(void**)(stats + 0x688)            ; game/UI object
eax = *(void**)(eax + 0x6118)             ; current local CPlayer
ecx = eax + 0x114                         ; CPlayer::goods
loop 40 times:
    if row visible:
        amount = FUN_00461FB0(ecx, *(int*)(stats + 0x8398 + i*4))
        update label
```

The second goods view (`FUN_0049B880` at `0x0049B880`) uses the same map and
the ids at `stats + 0x8390`, which are filled by `FUN_0049C270`. Both are views
of the same `CPlayer::goods` map. The Statistics screen and the Lua
`pl_getGoodAmount` therefore read the **same values**.

Runtime confirmation: `Probe.exe` in a loaded match printed:

```text
map[09] key=0xCD6EC133 (wood)  value=24
map[08] key=0xC5C945A3 (plank) value=44
map[29] key=0x55E952D3 (stone) value=68
map[33] key=0x69C15033 (hammer) value=16
```

These matched the visible Inventory screen.

## 6. Goods enum

Source: decrypted `data/scripts/properties/Goods.lua` from the tested
installation (`properties.gp_create(...)`). Group entries (`food`, `tools`,
`weapons`) are not stock entries and are excluded from the HUD.

| Key | Id | Icon X,Y |
|---|---|---:|
| wood | `0xCD6EC133` | 0,0 |
| plank | `0xC5C945A3` | 1,0 |
| stone | `0x55E952D3` | 2,0 |
| pig | `0xBB5D0683` | 3,0 |
| corn | `0x17626A03` | 4,0 |
| flour | `0xE70C72D3` | 5,0 |
| fish | `0x4012E5A3` | 6,0 |
| flesh | `0xAD0D6C63` | 7,0 |
| bread | `0x90AB8923` | 8,0 |
| water | `0xA9676263` | 9,0 |
| beer | `0x58647DA3` | 0,1 |
| ironOre | `0xEC5020BE` | 1,1 |
| gold | `0x4F41C633` | 2,1 |
| coal | `0x7068DCD3` | 3,1 |
| iron | `0x8198BF23` | 4,1 |
| coin | `0x499D4C63` | 5,1 |
| gripper | `0x6CC2D2D3` | 6,1 |
| axe | `0xE1281613` | 7,1 |
| saw | `0x3788FFC3` | 8,1 |
| pickaxe | `0x45D899E3` | 9,1 |
| hammer | `0x69C15033` | 0,2 |
| shovel | `0x67C04E73` | 1,2 |
| pot | `0xED76AE73` | 2,2 |
| fishingRod | `0xF3F3DCC3` | 3,2 |
| scythe | `0x1CEACD43` | 4,2 |
| hatchet | `0xF2B7453E` | 5,2 |
| rollingPin | `0xC2049323` | 6,2 |
| spear | `0x79F34393` | 7,2 |
| sword | `0xA8483903` | 8,2 |
| shield | `0x1EEBCF03` | 9,2 |
| boat | `0xF63DE553` | 0,3 |
| ship | `0x1D66B4AE` | 1,3 |
| donkey | `0x2448356E` | 5,3 |

### Icons

The inventory uses `data/gfx/uiElements1.dds` (encrypted with the `rc00`
container). Decrypted format:

- 512 × 512, DXT5, 10 mip levels;
- 48 × 48 cells, 10 columns × 5 rows;
- cell coordinates come from `properties.gp_setIconXY(good, x, y)`.

The DLL decrypts the DDS in memory at runtime and loads it with
`D3DXCreateTextureFromFileInMemory`; no game file is modified.

## 7. Update semantics

- `CPlayer::goods` is updated by the game's logic; the HUD only reads it.
- The HUD samples every 500 ms on the render thread.
- `+N/-N` is `current - previous` between two samples. It is a net change, not
  verified production/consumption.
- The map contains the stock counted by the original Inventory screen
  (town hall/castle and warehouses). Goods in transit or in production are not
  exposed as a separate value by this map.
- The HUD is hidden when there is no active local player (`active == 0` or
  `type != 2`), i.e. in menus and while a match is loading.

## 8. Confidence summary

| Finding | Confidence |
|---|---|
| `pl_getGoodAmount` registration/implementation | VERIFIED |
| `0x006CAC60` manager global | VERIFIED (disassembly + runtime) |
| `manager + 0x14` players array | VERIFIED |
| player stride 500, index +4, type +0xC, active +0x164 | VERIFIED |
| `player + 0x114` goods map | VERIFIED |
| map node layout / key / value | VERIFIED (disassembly) |
| Statistics screen uses the same map | HIGH (code path + runtime values) |
| map excludes transit/production stock | MEDIUM/HIGH; matches the screen, but not separately tested for every edge case |

## 9. Non-goals / unresolved

- No writable inventory access is used.
- The HUD does not manage depots, production or transport.
- The base `s2dng.exe` build has not been separately mapped; only
  `s2dng_addon.exe` is supported.
- The exact behavior for multiplayer/observer clients should be tested
  separately; the local-user scan uses `type == 2`.

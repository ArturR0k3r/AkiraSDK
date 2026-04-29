# Glasses Mystery

A top-down RPG for Akira Console, inspired by Zelda 1, Fallout 1 and Stardew Valley.

Explore the mysterious town of **Glasses** — a small settlement plagued by strange anomalies, gravitational distortions, and unexplained phenomena. Uncover the truth behind the town's secrets hidden in an abandoned lab beneath the old mansion.

## Controls

| Button | Action |
|--------|--------|
| D-Pad  | Move player |
| A      | Interact (NPCs, doors, items) / Advance dialogue / Confirm |
| B      | Back / Close menu |
| Settings | Pause menu |

## How to Exit

Press **Settings** to open the pause menu, then select **Exit** to return to the supervisor/launcher.

## Gameplay

- **Explore** 34 interconnected screens: town, forests, beaches, caves, and a mansion
- **Talk to NPCs**: Mayor, shopkeeper, librarian, fisherman, and more
- **Collect items**: diary pages, compass, lantern, crystals, keys
- **Complete quests**: find the old man's cabin, retrieve lost books, help the fisherman
- **Discover the lab**: find a keycard and uncover the anomaly research facility
- **Choose your ending**: Seal the anomalies, harness their power, or destroy everything

## Map Overview

```
 Forest NW — Forest N — Forest NE — Mansion Ext
     |           |                       |
  Lake W    Forest W — Town NW — Town NE — Mansion East
     |          |         |         |          |
  Lake E     Park    Town SW — Town SE — Forest Path
                |         |         |          |
           Cave Entry  Beach W — Beach E — Docks
                                              |
                                         Forest Deep
```

Indoor locations: Your House, Town Hall, Shop, Library, Old Cabin, Mansion (Hall/Study/Basement), Cave system, Anomaly Chamber, Lab.

## Building

Requires WASI SDK at `/opt/wasi-sdk`.

```sh
cd AkiraSDK/wasm_apps/glasses_mystery
make
```

Or build all apps:

```sh
cd AkiraSDK/wasm_apps
./build.sh
```

Output: `glasses_mystery.wasm` (~45 KB, fits in 128 KB WASM memory).

## Technical Details

- 320×240 RGB565 display, 8×8 pixel tiles (40×30 grid)
- 16×16 pixel player and NPC sprites
- RLE-compressed tile maps
- ~50 fps target with `delay()` frame pacing
- Save/load via AkiraOS storage API
- 5 source files: `main.c`, `engine.c`, `world.c`, `entities.c`, `dialogue.c`

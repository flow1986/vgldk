# Session summary: Pacman, Raycast and Dungeon Bash

Date: 2026-09-18

## Pacman

- `examples/pacman/pacman.c` was changed from a full maze redraw every tick to incremental dirty rendering.
- The first frame draws the complete maze. Later frames redraw only old and new actor cells and changed HUD values.
- This reduces visible framebuffer writes and was intended to reduce sprite flicker.
- The explicit busy-wait tick delay was increased because removing the redundant redraw made the game run much faster on real hardware.
- Current value: `FRAME_DELAY 3000`.
- Pacman cart build succeeded; the 8 KiB cart was 6312 bytes.

## Raycast

- `examples/raycast/raycast.mahnke.c` enables `GFX_BLOCKY` for the GL6000SL.
- This renders eight horizontal LCD pixels as one byte and reduces the ray columns from 240 to 30.
- Keyboard input no longer uses blocking `getchar()`.
- It now calls `keyboard_update()` and checks the held-key list via `keyboard_pressed[]` and `KEY_CODES[]`.
- WASD and cursor keys work continuously while held.
- Q/E strafe left/right; N/M remain as alternative strafe keys.
- Raycast cart build succeeded; the 16 KiB cart was 6977 bytes.

## Dungeon Bash investigation

- The archive `dungeonbash-1.7.tar` was added to the repository root by the user and is currently untracked.
- It contains a small C Unix terminal roguelike: `dungeonbash.c`, `Makefile`, `README`, `dungeonbash.6`, and `COPYING`.
- The license is GPL.
- The game uses Unix terminal/POSIX APIs such as `termios`, `read`, `write`, `ioctl`, `select`/similar input handling, ANSI output, and dynamic allocation.
- It is not a drop-in VGLDK/SDCC port, but its dungeon generation, visibility, monster, combat, item, and inventory logic are potential porting sources.
- A VGLDK port would need fixed arrays or pools, VGLDK keyboard polling, tile/framebuffer rendering, target-specific timing/randomness, and removal of Unix APIs.
- The GPL license must be respected for any derived port.

## Build and Git notes

- Pacman build command: `cd examples/pacman && make cart`.
- Raycast build command: `cd examples/raycast && make cart`.
- Existing non-fatal build warnings remain in `include/arch/gl6000sl/lcd.h` and optimizer flow diagnostics in the raycast build.
- `origin` is the user's fork `flow1986/vgldk`; `upstream` is `hotkeymuc/vgldk`.
- The Dungeon Bash tar archive is intentionally not included in the Pacman/Raycast commit.
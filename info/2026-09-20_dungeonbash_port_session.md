# Session summary: Dungeon Bash port for the GL6000SL

Date: 2026-09-20

## Starting point

- The user pointed at `dungeonbash-1.7.tar` (already sitting at the repo root from a
  previous session, extracted and git-committed as loose `.c`/`.h` files) and asked
  whether it can reasonably be ported to the VGLDK/GL6000SL as a new `dungeonbash`
  example, with graphics done via 8x8 sprites instead of terminal/ASCII output.

## Feasibility discussion

- Dungeon Bash (Martin Read, 2005, GPLv3) is a curses/POSIX terminal roguelike: dynamic
  memory, `termios` raw input, and a dungeon level kept as five parallel `int[42][42]`
  arrays - none of that survives unchanged on a Z80 cart with banked RAM.
- Viewport/camera: the original's "scrolled, windowed copy of a back-buffer centered on
  the player" (`display.c`: `draw_world()`/`newsym()`) maps 1:1 onto 8x8 sprite tiles -
  same camera math, but the window had to shrink from 21x21 characters (168x168px) to
  30x10 tiles (240x80px) to fit the GL6000SL's 240x100 1bpp LCD.
- Full monster/magic/inventory/multi-level parity was assessed as out of reach for an
  8KB cart (26 monster types + 31 object types + full to-hit/damage-type combat +
  spellcasting is several thousand lines beyond the MVP) and even ambitious for a 32KB
  one; multiple dungeon *levels* are cheap RAM-wise (regenerate on descend) but the code
  size for full magic/inventory is not. Recommended an incremental approach instead of a
  full 1:1 port.

## examples/dungeonbash/ (new)

- Created from scratch (not a line-by-line port): scrolling camera over a `48x24` byte
  terrain grid, simplified room+corridor dungeon generator, turn-based bump combat.
- 10 monster types (of the original 26): Newt, Rat, Snake, Wolf, Thug, Goblin, Goon,
  Hunter, Zombie, Troll - each with a hand-drawn 8x8 sprite and hp/atk/def roughly
  following `permons.c`'s power ordering.
- First slice of inventory/magic: weapons (dagger/sword) and armour (leather/chain)
  auto-equip on pickup if better; healing potions and teleport scrolls as simple
  consumable counters (not the original's full 19-slot inventory).
- Combat messages now show the monster's name and current/max HP dynamically
  (`msg_clear()`/`msg_append()`/`msg_append_num()` - no `sprintf` on this toolchain).
- Added a difficulty-weighted score (`+10 + hpmax + atk*2` per kill) and an in-session
  high score shown in the HUD.
- Game Over no longer hangs: shows "GAME OVER! HI:<high_score>" and restarts on any
  keypress (`x`/`X` quits instead).
- Multiplayer: `players[]` array + `active_player` index from the start, but real
  multiplayer is explicitly planned (NOT implemented) as an ESP8266 on the
  parallel/printer port talking to a dedicated external multiplayer server - not
  split-screen, not hot-seat.
- Grew from an 8KB to a 32KB cart (`CART_SIZE_KB=32`) as content was added; currently
  ~18.9KB/32KB used.

## Hardware side-questions (discussion only, no code)

- Whether a cart can exceed 32KB depends on the OneROM's own capacity/footprint AND on
  whether the physical cart-slot connector actually routes the extra high address lines
  through to the chip socket - confirmed real hardware supports bank-switching up to
  512KB-1MB (see `examples/agi`'s `CART_SIZE_KB=1024` + `vagi_bank.h`), but that's a
  software mechanism only if the wiring is already there; otherwise it needs rewiring
  the cart PCB (not the console). Left as an open question for the user to verify with
  a continuity check on their own hardware.

## Repo housekeeping

- Moved the 21 already-extracted `dungeonbash-1.7` source files (`bmagic.c`, `combat.c`,
  `display.c`, `dunbash.h`, `main.c`, `map.c`, `misc.c`, `mon2.c`, `monsters.c`,
  `objects.c`, `permobj.c`, `permons.c`, `pmon2.c`, `rng.c`, `u.c`, `vector.c`, plus their
  own `Makefile`/`notes.txt`) and `dungeonbash-1.7.tar` out of the repo root into
  `dungeonbash_original/` (`git mv`, preserved as renames). Root `LICENSE` and
  `README.md` were left in place - they belong to the vgldk repo itself, not to the
  dungeonbash import (confirmed via separate/older git history).

## Build notes learned this session

- `stdiomin.h` has no `<string.h>`/`strcpy()` - SDCC errors with "too many parameters"
  (implicit declaration) if called; write small manual byte-copy helpers instead.
- `tools/Makefile.mk`'s `CART_SIZE_KB` only ships EEPROM-burn metadata for 8 and 32, but
  the value itself is a free `dd` cut size - other examples already use 16
  (`raycast`) or even 1024 (`agi`, with manual bank switching).

# Session summary: VGL BASIC example (line-numbered BASIC interpreter)

Date: 2026-09-22

## Starting point

The user asked for a new example: a simple/lightweight BASIC interpreter for the
GL6000SL, ported/inspired by looking at existing tiny BASIC interpreters, with
line numbers like classic 1980s home computers. Requirements: `PEEK`/`POKE`,
sound, simple drawing, common variables/arithmetic, and storage "im internen
RAM" (in internal RAM).

## examples/basic/ (new)

A from-scratch, non-tokenizing BASIC interpreter (`basic.c`): the whole program
is kept as plain text directly in a fixed RAM array (`program[2048]`, each line
`[2-byte lineno][text]['\0']`, sorted, terminated by a `0x0000` line number) -
statements are re-parsed from that text every time they execute instead of
being compiled to bytecode, matching the "no dynamic memory" approach used by
every other example in this repo.

Execution model: `GOTO`/`GOSUB`/`RETURN`/`NEXT`/`IF...THEN <line>` all work by
having the statement set a global `jump_flag`/`jump_target` pointer that the
main run loop picks up next. `FOR`/`GOSUB` deliberately resume at the start of
the *next* program line rather than mid-line (computed via `next_line_ptr()`),
so `FOR ... TO ... STEP` must be the last statement on its line - a
simplification to avoid tracking mid-line resume points.

Supported over the course of the session (several follow-up rounds):
* `LET` (optional)/`PRINT`/`INPUT`, `IF...THEN <line> [ELSE <line>] | <stmt>`,
  `GOTO`, `GOSUB`/`RETURN`, `FOR`/`TO`/`STEP`/`NEXT`, `REM`, `END`/`STOP`
* `PEEK(addr)`/`POKE addr,val`, `PLOT x,y[,c]`/`LINE x0,y0,x1,y1` (direct
  framebuffer bit-set, same technique as `examples/pacman`/`roguelike` sprite
  blitting), `SOUND freq,len`/`BEEP`, `CLS`
* `RND(n)`/`ABS(n)` expression functions
* `KEY()` - **non-blocking** key read (just `keyboard_inkey()`, 0 if nothing
  pressed) so BASIC games can poll input without stopping the program, plus
  `PAUSE n` (busy-wait, no hardware timer on this platform) for simple game
  loop timing
* `LIST`/`RUN`/`NEW`, `EDIT n` (re-opens a line for editing instead of
  retyping it), `SAVE`/`LOAD` (a backup/undo slot in a second RAM buffer -
  explicitly NOT real EEPROM/filesystem persistence)
* A custom line editor (`input_line()`) replacing plain `gets()`: LEFT/RIGHT
  arrow cursor movement and a blinking cursor block, both built by polling the
  already non-blocking `keyboard_inkey()` in a busy-wait loop (no interrupts
  needed) and XOR'ing a character cell (`invert_cell()`, works for any font
  width incl. non-8-bit-aligned ones like the default 4x6 font)
* `CSAVE [n]`/`CLOAD [n]` - stream the program as plain text over the
  parallel port via the existing bit-banged `include/driver/softuart.h`
  (9600 baud), prefixed with a 2-byte `S`/`L` + slot-digit (`0`-`9`) header so
  an external device can auto-route to the right "slot" without needing a
  web UI click at the exact transfer moment

`examples/basic/README.md` documents every statement/function/command (the
one example in this repo with its own README).

## External storage: examples/basic/basic_serial.py and esp_basic_store/

Two ways to get a program on/off real hardware, since CSAVE/CLOAD needs a
listener on the physical parallel port (MAME does not emulate that wiring, so
`make emu` cannot be used for CSAVE/CLOAD testing):

* `basic_serial.py`: a small pyserial host-side script for a plain USB-serial
  adapter (FTDI/CP2102, no microcontroller needed) - `receive`/`send`
  subcommands, reads/discards the 2-byte slot prefix.
* `esp_basic_store/` (`esp_basic_store.ino` + its own README): an Arduino
  sketch, written for a Wemos D1 Mini (ESP8266) as requested, that acts as a
  standalone always-on 10-slot program store: LittleFS files per slot, WiFi
  `WIFI_AP_STA` mode (always-on AP `VGLBASIC-Setup` + optional join of an
  existing WLAN via a `/wifi` web form), and a small browser UI per slot
  (view/edit/download/delete). Compiles for ESP8266 *or* ESP32 via
  `#if defined(ESP8266)/ESP32` and also has a genuine `#else` fallback for a
  plain AVR Arduino (Uno/Nano/Mega) - EEPROM-backed, only 4 slots of ~250
  bytes, no WiFi/web UI, since that's a hardware limitation (no WiFi chip, 1KB
  EEPROM), not a software choice; documented as such rather than overclaiming
  full parity. **Not tested on real hardware** - no ESP toolchain/device was
  available in this session's sandbox; wiring notes flag that ESP8266/ESP32
  GPIOs are 3.3V and NOT 5V tolerant (unlike the FTDI adapters used elsewhere
  in this repo), so the VGL(5V)->ESP RX line needs a resistor divider or
  proper level shifter - untested which is strictly necessary/sufficient.

## Gotchas / lessons learned this session

* SDCC has no automatic forward visibility across a `.c` file: a function
  defined *before* another function it calls must still have that callee's
  prototype declared even earlier, up in a top forward-declarations block.
* A recurring red herring: `tools/calcsize.py`'s reported cart "Used space"
  byte count stayed bit-for-bit identical (`20641` bytes) across several
  builds that each added real new code (verified by checking `out/basic.map`/
  `.sym` for the new function addresses/symbols, which *did* change) - the
  size metric it prints is apparently not a reliable "did anything change"
  signal; don't trust it alone when sanity-checking a rebuild, check the
  symbol table/map file instead if in doubt.
* When adding a wire protocol prefix (the `S`/`L` + slot-digit bytes) to an
  existing stream-based command, remember to update *all* consumers that
  parse that stream - `basic_serial.py`'s `receive()` needed to read+discard
  the new 2-byte prefix, otherwise it would have silently corrupted the start
  of every saved `.bas` file with 2 garbage characters.

## Build status

`examples/basic` builds cleanly as a 32KB cart (`CART_SIZE_KB = 32`, ~20KB
used) - `LET`(optional)/`FOR`/`GOSUB`/expression-parser/stacks no longer fit
an 8KB one. Verified with `make cart` after every round of changes in this
session; only warning is a pre-existing, unrelated one from
`include/arch/gl6000sl/lcd.h`. `esp_basic_store.ino` could not be compiled in
this session (no ESP8266/ESP32 Arduino core installed in the sandbox).

## Follow-up: keyboard symbols and manual sprite test

The GL6000SL symbol-shift codes for the comma and period keys were verified on
hardware: 204 is mapped to `<` and 206 to `>`. `examples/basic/basic.c` now
translates these codes in BASIC keyboard input paths so comparison operators
can be entered in the line editor and returned by `KEY()`.

The built-in LOAD seed programs were removed again after testing because they
made the real cartridge unusable on the target. LOAD is therefore only the
ordinary RAM backup slot; no demo is embedded in the cartridge.

Minimal manual sprite test (not embedded in the cart):

```basic
10 CLS
20 DEFSPRITE 0,0,18
30 DEFSPRITE 0,1,3C
40 DEFSPRITE 0,2,7E
50 DEFSPRITE 0,3,FF
60 DEFSPRITE 0,4,FF
70 DEFSPRITE 0,5,7E
80 DEFSPRITE 0,6,3C
90 DEFSPRITE 0,7,18
100 MOVESPRITE 0,0,40
110 MOVESPRITE 0,SPRITEX(0)+2,SPRITEY(0)
120 PAUSE 20
130 GOTO 110
```

This deliberately short example moves the sprite automatically from left to
right and is intended to be typed manually on the VTech.

## Follow-up: paged LIST output

`LIST` now pauses after every 15 program lines and displays `-- MORE --`.
Any key continues the listing; the interrupt/ESC key stops it. This prevents
long programs from scrolling past the entire screen before they can be read.

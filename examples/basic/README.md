# VGL BASIC

A small line-numbered BASIC interpreter for the VTech Genius LEADER 6000 SL
(and compatible models), in the style of classic 1980s home-computer BASICs:
type a line starting with a number to store it in the program, type a line
without one to execute it immediately.

The whole program lives as plain text directly in RAM ("im internen RAM") -
no tokenizer, no compilation step; each statement is simply re-parsed every
time it runs. This keeps the interpreter small and needs no dynamic memory.

Build like any other cart example:
```
cd examples/basic
make cart
make emu     # requires MAME + ROMs, see repo root README
```

## Quick start

```
10 PRINT "HELLO WORLD"
20 FOR I = 1 TO 5
30 PRINT I
40 NEXT I
RUN
```

Type `LIST` to see the stored program, `RUN` to execute it, `NEW` to erase it.

## Line editor

While typing a line (program line or immediate command) you get a small
line editor, not just a dumb input field:

| Key            | Effect                                    |
|----------------|-------------------------------------------|
| LEFT / RIGHT   | Move the cursor within the line           |
| any character  | Insert at the cursor position             |
| Backspace      | Delete the character before the cursor    |
| Delete/Insert  | Delete the character at/after the cursor  |
| Enter          | Accept the line                           |

The cursor blinks (an inverted character cell) while you're not typing, done
by polling the keyboard in a busy-wait loop - there's no hardware timer on
this platform. `EDIT n` re-opens an existing program line in this editor
instead of forcing you to retype it from scratch.

Note: the editor assumes the line fits on one screen row (no line-wrap-aware
cursor math) - fine for the default font (60 columns) and typical BASIC lines.

## Variables

26 signed 16-bit integer variables, named `A` to `Z`. No strings, no arrays.
Assignment works with or without `LET`:
```
10 LET A = 5
20 B = A * 2
```

## Expressions

Operators (usual precedence): `+ - * / MOD`, comparisons `= <> < > <= >=`
(evaluate to 1/0), unary `-`/`+`, parentheses `( )`.

Functions:
| Function    | Description                                              |
|-------------|-----------------------------------------------------------|
| `PEEK(addr)`| Read one byte from memory address `addr`                  |
| `RND(n)`    | Pseudo-random integer, `0 .. n-1`                          |
| `ABS(n)`    | Absolute value                                             |
| `KEY()`     | **Non-blocking** key read - see "Real-time input" below    |

## Statements

| Statement                                | Description                                                   |
|-------------------------------------------|----------------------------------------------------------------|
| `LET var = expr` (or just `var = expr`)   | Assign a variable                                              |
| `PRINT expr, "text", ...`                 | Print values/strings. `,` = space, `;` = no separator/newline   |
| `INPUT var[, var...]`                     | Prompt `?` and read a number into each variable (**blocking**) |
| `IF expr THEN line [ELSE line]`           | Conditional jump to a line number, with optional `ELSE`         |
| `IF expr THEN stmt`                       | Conditional inline statement (no `ELSE` in this form)           |
| `GOTO line`                               | Jump to a line number                                           |
| `GOSUB line` / `RETURN`                   | Subroutine call/return (8 levels deep)                          |
| `FOR var = a TO b [STEP s]` / `NEXT [var]`| Loop (6 nested levels deep). `FOR` must be the last statement on its line |
| `POKE addr, val`                          | Write one byte to memory                                        |
| `PLOT x, y [, c]`                         | Set (`c`=1, default) or clear (`c`=0) one pixel                 |
| `LINE x0, y0, x1, y1`                     | Draw a line between two points                                  |
| `CLS`                                     | Clear the screen                                                 |
| `SOUND freq, len` / `BEEP`                | Play a tone/beep (currently the fixed stock beep sound, see code)|
| `PAUSE n`                                 | Busy-wait delay, roughly milliseconds (approximate, untuned)     |
| `REM ...`                                 | Comment, rest of the line is ignored                             |
| `END` / `STOP`                            | Stop the running program                                         |
| `CSAVE [n]` / `CLOAD [n]`                 | Send/receive the program as text over the parallel port, slot `n`=0-9 (default 0) |
| `:`                                       | Separates multiple statements on one line                        |

## Immediate commands

These only make sense typed directly at the `>` prompt (not inside a stored
program), except `LIST`/`NEW`/`SAVE`/`LOAD` which also work as statements:

| Command     | Description                                                       |
|-------------|--------------------------------------------------------------------|
| `RUN`       | Start executing the stored program from the lowest line number     |
| `LIST`      | List the stored program, pausing every 15 lines at `-- MORE --`   |
| `NEW`       | Erase the stored program and reset the FOR/GOSUB stacks            |
| `EDIT n`    | Re-open line `n` in the line editor instead of retyping it          |
| `SAVE`      | Copy the current program into a backup RAM slot                    |
| `LOAD`      | Restore the program from that backup slot                          |
| `CSAVE [n]` | Send the program as text over the parallel port, slot `n`=0-9 (default 0) |
| `CLOAD [n]` | Receive a program as text over the parallel port, slot `n`=0-9 (default 0) |

`SAVE`/`LOAD` are a lightweight backup/undo slot in RAM, **not** real
EEPROM/filesystem persistence - there is currently no verified persistent
storage driver for this hardware. The program (and the SAVE slot) is only
as safe as the machine's RAM stays powered. `CSAVE`/`CLOAD` (see below) are
the way to get a program off/onto the device for real, e.g. onto an
Arduino/ESP.

`LIST` pauses after every 15 lines and waits for a key before continuing.
Press the interrupt/ESC key at `-- MORE --` to stop listing early.

## Real-time input for games

`keyboard_getchar()`/`INPUT` wait for a keypress (and Enter, for `INPUT`).
For games you usually want to check "is a key currently being pressed?"
without stopping the program - that's what `KEY()` is for: it returns 0 if
nothing is pressed right now, otherwise the charcode of the key, and never
waits. Example real-time-ish loop (still cooperative - BASIC has no
interrupts - but `KEY()` itself never blocks):

```
10 X = 15
20 CLS
30 PLOT X, 10
40 K = KEY()
50 IF K = 100 THEN X = X + 1
60 IF K = 97 THEN X = X - 1
70 PLOT X, 10, 0
80 PAUSE 50
90 GOTO 30
```
(`100` = `d`, `97` = `a` - print `PRINT KEY()` once with a key held down if
you want to look up other charcodes; there are no character/string literals
for keys in expressions, only their numeric codes.)

## Sending/receiving programs with CSAVE/CLOAD

`CSAVE [n]`/`CLOAD [n]` stream the program as plain text over the parallel
port, using the existing bit-banged `softuart` driver (same physical layer
already proven for `make upload`/the monitor and CP/M examples). `n` is an
optional slot number `0`-`9` (default `0`), sent as a 2-byte prefix
(`S`/`L` + the digit) before the actual text - so an external device can
tell which of several stored programs to save/load without needing a web
UI click at the exact right moment (see `esp_basic_store/` below). Program
text is exactly what you'd `LIST`, one line at a time, ending with a
single `0x1A` (EOF) byte.

Two ways to use this:

* **`esp_basic_store/`** in this folder: an Arduino/ESP8266/ESP32 sketch
  (written for a Wemos D1 Mini) that acts as a standalone 10-slot program
  store with a WiFi + browser interface - wire it up once and leave it
  running, no PC needed. See `esp_basic_store/README.md` for wiring and
  setup.
* **`basic_serial.py`** in this folder: a plain USB-serial adapter (FTDI,
  CP2102, ... - **no microcontroller needed**) plus this PC-side helper
  script. Good for one-off transfers to/from a PC:
  1. Wire it up (see `include/arch/gl6000sl/softserial.h` for the full
     pinout): adapter RXD <-> any parallel port pin 2-9 (D0..D7), adapter
     TXD -> pin 11 (BUSY) through a ~1k resistor, adapter GND <-> pin
     18-25 (GND).
  2. `pip install pyserial`, then (adjust `PORT`/`BAUD` in the script if
     needed):
     ```
     python3 examples/basic/basic_serial.py receive myprogram.bas
     ```
  3. On the VGL: type `CSAVE` and press Enter - the script saves the
     stream to `myprogram.bas` once it sees the EOF byte.
  4. To send a program back: type `CLOAD` on the VGL first, *then* run
     `python3 examples/basic/basic_serial.py send myprogram.bas` and press
     Enter at the script's prompt. Press any key on the VGL to abort a
     `CLOAD` that's waiting.

Either way, this only works on real hardware (or anything that emulates
the physical parallel port pins) - MAME does not emulate that wiring, so
`make emu` cannot be used for `CSAVE`/`CLOAD` testing.

## Sprites

There are eight monochrome sprites (`0` to `7`). Each sprite is 8x8 pixels
large and each row uses one byte. The high bit is the leftmost pixel.
`DEFSP` sets one row using two hexadecimal digits. The sprite commands use
short names to make typing on the VTech keyboard quicker:

```
10 DEFSP 0,0,18
20 DEFSP 0,1,3C
30 DEFSP 0,2,7E
40 DEFSP 0,3,FF
50 DEFSP 0,4,FF
60 DEFSP 0,5,7E
70 DEFSP 0,6,3C
80 DEFSP 0,7,18
90 DRASP 0,40,20
```

`DEFSP` accepts both the compact hex form and eight binary digits. For
example, these two lines define the same row:

```
10 DEFSP 0,0,18
20 DEFSP 0,0,00011000
```

The complete conversion table is:

| Hex | Binary | Hex | Binary | Hex | Binary | Hex | Binary |
|-----|--------|-----|--------|-----|--------|-----|--------|
| 00 | 00000000 | 40 | 01000000 | 80 | 10000000 | C0 | 11000000 |
| 01 | 00000001 | 41 | 01000001 | 81 | 10000001 | C1 | 11000001 |
| 02 | 00000010 | 42 | 01000010 | 82 | 10000010 | C2 | 11000010 |
| 03 | 00000011 | 43 | 01000011 | 83 | 10000011 | C3 | 11000011 |
| 04 | 00000100 | 44 | 01000100 | 84 | 10000100 | C4 | 11000100 |
| 05 | 00000101 | 45 | 01000101 | 85 | 10000101 | C5 | 11000101 |
| 06 | 00000110 | 46 | 01000110 | 86 | 10000110 | C6 | 11000110 |
| 07 | 00000111 | 47 | 01000111 | 87 | 10000111 | C7 | 11000111 |
| 08 | 00001000 | 48 | 01001000 | 88 | 10001000 | C8 | 11001000 |
| 09 | 00001001 | 49 | 01001001 | 89 | 10001001 | C9 | 11001001 |
| 0A | 00001010 | 4A | 01001010 | 8A | 10001010 | CA | 11001010 |
| 0B | 00001011 | 4B | 01001011 | 8B | 10001011 | CB | 11001011 |
| 0C | 00001100 | 4C | 01001100 | 8C | 10001100 | CC | 11001100 |
| 0D | 00001101 | 4D | 01001101 | 8D | 10001101 | CD | 11001101 |
| 0E | 00001110 | 4E | 01001110 | 8E | 10001110 | CE | 11001110 |
| 0F | 00001111 | 4F | 01001111 | 8F | 10001111 | CF | 11001111 |
| 10 | 00010000 | 50 | 01010000 | 90 | 10010000 | D0 | 11010000 |
| 11 | 00010001 | 51 | 01010001 | 91 | 10010001 | D1 | 11010001 |
| 12 | 00010010 | 52 | 01010010 | 92 | 10010010 | D2 | 11010010 |
| 13 | 00010011 | 53 | 01010011 | 93 | 10010011 | D3 | 11010011 |
| 14 | 00010100 | 54 | 01010100 | 94 | 10010100 | D4 | 11010100 |
| 15 | 00010101 | 55 | 01010101 | 95 | 10010101 | D5 | 11010101 |
| 16 | 00010110 | 56 | 01010110 | 96 | 10010110 | D6 | 11010110 |
| 17 | 00010111 | 57 | 01010111 | 97 | 10010111 | D7 | 11010111 |
| 18 | 00011000 | 58 | 01011000 | 98 | 10011000 | D8 | 11011000 |
| 19 | 00011001 | 59 | 01011001 | 99 | 10011001 | D9 | 11011001 |
| 1A | 00011010 | 5A | 01011010 | 9A | 10011010 | DA | 11011010 |
| 1B | 00011011 | 5B | 01011011 | 9B | 10011011 | DB | 11011011 |
| 1C | 00011100 | 5C | 01011100 | 9C | 10011100 | DC | 11011100 |
| 1D | 00011101 | 5D | 01011101 | 9D | 10011101 | DD | 11011101 |
| 1E | 00011110 | 5E | 01011110 | 9E | 10011110 | DE | 11011110 |
| 1F | 00011111 | 5F | 01011111 | 9F | 10011111 | DF | 11011111 |
| 20 | 00100000 | 60 | 01100000 | A0 | 10100000 | E0 | 11100000 |
| 21 | 00100001 | 61 | 01100001 | A1 | 10100001 | E1 | 11100001 |
| 22 | 00100010 | 62 | 01100010 | A2 | 10100010 | E2 | 11100010 |
| 23 | 00100011 | 63 | 01100011 | A3 | 10100011 | E3 | 11100011 |
| 24 | 00100100 | 64 | 01100100 | A4 | 10100100 | E4 | 11100100 |
| 25 | 00100101 | 65 | 01100101 | A5 | 10100101 | E5 | 11100101 |
| 26 | 00100110 | 66 | 01100110 | A6 | 10100110 | E6 | 11100110 |
| 27 | 00100111 | 67 | 01100111 | A7 | 10100111 | E7 | 11100111 |
| 28 | 00101000 | 68 | 01101000 | A8 | 10101000 | E8 | 11101000 |
| 29 | 00101001 | 69 | 01101001 | A9 | 10101001 | E9 | 11101001 |
| 2A | 00101010 | 6A | 01101010 | AA | 10101010 | EA | 11101010 |
| 2B | 00101011 | 6B | 01101011 | AB | 10101011 | EB | 11101011 |
| 2C | 00101100 | 6C | 01101100 | AC | 10101100 | EC | 11101100 |
| 2D | 00101101 | 6D | 01101101 | AD | 10101101 | ED | 11101101 |
| 2E | 00101110 | 6E | 01101110 | AE | 10101110 | EE | 11101110 |
| 2F | 00101111 | 6F | 01101111 | AF | 10101111 | EF | 11101111 |
| 30 | 00110000 | 70 | 01110000 | B0 | 10110000 | F0 | 11110000 |
| 31 | 00110001 | 71 | 01110001 | B1 | 10110001 | F1 | 11110001 |
| 32 | 00110010 | 72 | 01110010 | B2 | 10110010 | F2 | 11110010 |
| 33 | 00110011 | 73 | 01110011 | B3 | 10110011 | F3 | 11110011 |
| 34 | 00110100 | 74 | 01110100 | B4 | 10110100 | F4 | 11110100 |
| 35 | 00110101 | 75 | 01110101 | B5 | 10110101 | F5 | 11110101 |
| 36 | 00110110 | 76 | 01110110 | B6 | 10110110 | F6 | 11110110 |
| 37 | 00110111 | 77 | 01110111 | B7 | 10110111 | F7 | 11110111 |
| 38 | 00111000 | 78 | 01111000 | B8 | 10111000 | F8 | 11111000 |
| 39 | 00111001 | 79 | 01111001 | B9 | 10111001 | F9 | 11111001 |
| 3A | 00111010 | 7A | 01111010 | BA | 10111010 | FA | 11111010 |
| 3B | 00111011 | 7B | 01111011 | BB | 10111011 | FB | 11111011 |
| 3C | 00111100 | 7C | 01111100 | BC | 10111100 | FC | 11111100 |
| 3D | 00111101 | 7D | 01111101 | BD | 10111101 | FD | 11111101 |
| 3E | 00111110 | 7E | 01111110 | BE | 10111110 | FE | 11111110 |
| 3F | 00111111 | 7F | 01111111 | BF | 10111111 | FF | 11111111 |

For movement, `MOVSP n,x,y` clears the old position and draws the sprite
at the new one. `SPX(n)` and `SPY(n)` return its current position and can be
used in `IF` collision checks. The longer names `SPRITEX(n)` and `SPRITEY(n)`
are also accepted for compatibility.

Sprite abbreviations:

| Short form | Meaning |
|------------|---------|
| `DEFSP` | Define one 8-pixel sprite row |
| `DRASP` | Draw a sprite at an X/Y position |
| `MOVSP` | Erase and move a sprite to an X/Y position |
| `SPX(n)` | Read the current X position of sprite `n` |
| `SPY(n)` | Read the current Y position of sprite `n` |

## Design notes / limitations

* Program storage: each line is `[2-byte line number][text]['\0']`, kept
  sorted by line number in a fixed `program[2048]` RAM array, terminated by
  a `0x0000` line number. `SAVE`/`LOAD` just copy this whole buffer to/from
  a second same-size buffer.
* `GOTO`/`GOSUB`/`RETURN`/`NEXT` redirect execution by pointing the run loop
  at a different line; `FOR`/`GOSUB` resume at the start of the *next*
  program line (not mid-line) - hence `FOR ... TO ... STEP` must be the last
  statement on its line.
* No strings/arrays, no multi-dimensional variables, no user-defined
  functions/procedures beyond `GOSUB` - kept intentionally small ("ein
  leichtes BASIC").

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
| `LIST`      | List the stored program                                            |
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

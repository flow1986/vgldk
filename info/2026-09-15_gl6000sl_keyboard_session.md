# GL6000SL Keyboard Session - 2026-09-15

## Confirmed Fix

The GL6000SL keyboard row-select helper in `include/arch/gl6000sl/keyboard.h`
was broken with the installed SDCC 4.2.0 compiler.

`keyboard_matrix_out(byte a)` was declared `__naked` and fetched its parameter
from `SP+2`. SDCC actually passes a single byte argument to this function in
register A, without pushing it on the stack. The helper therefore sent stale
stack data to keyboard port `0x40`, resulting in unresponsive keys, ghosted
scancodes, and the raycast demo often turning left regardless of the pressed
key.

The helper now writes the incoming A register directly to port `0x40`. This was
confirmed on a real German GL6000SL: the keyboard test and freshly built raycast
cart respond to keyboard input after the change.

## German Keyboard Mapping

The live keyboard test cart was added in `examples/keyboard_test_gl6000sl/`.
It displays the raw matrix state, scancode, mapped keycode and decoded charcode.
It was used to confirm the following physical key positions:

| Scancode | Key label / mapping |
| --- | --- |
| `0x04` | `ue` |
| `0x06` | Shift |
| `0x14` | Insert / Delete |
| `0x1c` | Shift Lock |
| `0x2a` | Break / Escape |
| `0x2d` | `oe` |
| `0x2f` | Help / Player 1 |
| `0x35` | `ae` |
| `0x37` | Print / Symbol |
| `0x38` | OFF |
| `0x3f` | Answer |
| `0x48` | Alt |
| `0x50` | Repeat |
| `0x68` | Right / Player 2 |

The three umlauts were corrected in `KEY_CODES[]`. Shift mappings for
Help/Player 1, Right/Player 2, Insert/Delete, and uppercase umlauts were added.

The red calculator labels on letter keys were transcribed into a preliminary
`KEY_MAP_ALT[]` table. The ALT modifier was not implemented before this session.
This mapping remains unverified on real hardware and may need adjustment.

## Font Status

The keyboard test enables `FONT_FULL_ASCII` before including `vgldk.h`, so the
4x6 font includes its CP437 glyph range. On the real device, umlaut and math
codes still rendered blank in the test. Keyboard mapping is unaffected; the
LCD/font rendering path remains an open issue.

## OFF Key Status

The OFF key is visible as normal matrix scancode `0x38`.

The old note `OUT 0x0B,0x00` was tested and did not power off the German
GL6000SL. Examination of the stock `gl6000sl` ROM also found no direct use of
port `0x0B`.

Another existing reverse-engineering note suggested `OUT 0x55,0xFC`. A
test-local implementation was added to the keyboard test cart. It causes a
brief screen blank/flicker, but the system immediately comes back. Following it
with `DI` and an infinite `HALT` loop did not change the result. It is not in the
shared keyboard driver. The actual power-latch protocol remains unknown.

For further work, capture the real machine while the stock firmware handles OFF
with a logic analyser: `A0-A7`, `D0-D7`, `/IORQ` and `/WR`, triggered on an I/O
write. Compare the final writes before OFF with normal operation. MAME models
the keyboard matrix but not the power latch, so it cannot verify the final power
operation alone.

## MAME and Tools

The user-provided, legally owned `gl6000sl.zip` ROM set was verified by MAME and
placed at `~/mame/roms/gl6000sl.zip` in this Codespace (outside the Git repo).

`tools/mame_debug/keyboard_trace.lua` plus `run_trace.sh` trace accesses to
keyboard ports `0x40-0x43`. MAME and Xvfb were installed to run this headlessly.
The trace confirmed the stock firmware's active-low row scan on port `0x40` and
reads from `0x41`/`0x42`. It did not reveal the external power-latch protocol.

Installed build dependencies in this Codespace:

* `sdcc` 4.2.0 (includes `sdasz80`)
* `bc` (required by `tools/Makefile.mk` address arithmetic)
* OneROM CLI 0.4.0 (`onerom`)
* `mame`, `xvfb`, `z80dasm`

`make cart` must have `bc` available. Without it, `LOC_CODE_MAIN` incorrectly
became `0x0000` and the produced cartridge image was invalid/tiny without a
clear build failure.

## OneROM and RAM

The queried OneROM CLI exposes ROM-slot programming only. It does not document
a mode to emulate writable SRAM alongside a ROM image. For runtime data, the
GL6000SL itself appears to provide banked internal RAM at `0xc000-0xdfff`,
selected through port `0x53`; notes in `examples/agi/2do_vtech_AGI.txt` estimate
about 29 KiB usable RAM in total. It is volatile across power-off.

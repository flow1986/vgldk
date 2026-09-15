![VGLDK Logo](/info/VGLDKLogo.svg)

VTech Genius Leader Development Kit - an unofficial software development kit (SDK) for Z80 based VTech "Genius LEADER" learning computers (aka. "PreComputer", "YENO MisterX", "Genio" or "Compusavant").
This project is in no way affiliated with the VTech company. This is a pure hobby project, everything is provided as-is.

This project is still heavily *work in progress*, but quite usable. For some working proof-of-concept stuff, visit the [Hackaday project page](https://hackaday.io/project/166921-v-tech-genius-leader-precomputer-hacking) and get started using the zipped snapshots of "VGL CP/M" and other SDCC code examples.

![CP/M running on GL4000](/info/CPM_on_GL4000_small.jpg)

## Target platforms
	pc1000
		- VTech PreComputer 1000 [US/UK]
		- YENO Mister X [DE]
	gl2000
		- VTech PreComputer 2000 [US/UK]
		- VTech Genius LEADER 2000 [DE]
		- VTech Genius LEADER 2000 PLUS [DE]
		- VTech Genius LEADER 2000 Compact [DE]
		- VTech PreComputer THINK BOOK [UK]
		- YENO Mister X2 [DE]
	gl4000
		- VTech Genius LEADER 4000 Quadro [DE]
		- VTech Genius LEADER 4004 Quadro L [DE]
		- VTech PreComputer POWER PAD [UK]
	gl6000sl
		- VTech Genius LEADER 6000 SL [DE]
		- VTech Genius LEADER 7007 SL [DE]
		- VTech PC PRESTIGE [US]
		- VTech PC Navigator [US]
		- (YENO COMPUSAVANT [FR])

Newer models ("CX" series, circa 1999 onward) use a completely different system architecture, namely a CompactRISC CR16B CPU. Refer to the [VCXDK](https://www.github.com/hotkeymuc/vcxdk) for those models.

## Files
* examples: Some demonstrative use cases and experiments
* include: Hardware drivers and a rudimentary libc environment
* tools: Scripts that help development

## Recent fix: gl6000sl keyboard driver returned garbage row-select values
The `keyboard_matrix_out()` function in `include/arch/gl6000sl/keyboard.h` (used by every GL6000SL/7007SL/Prestige
keyboard read - `keyboard_ispressed()`, `keyboard_update()`, `keyboard_inkey()`, ...) fetched its byte parameter
from the stack (`ld hl,#2; add hl,sp; ld a,(hl)`), assuming SDCC always pushes function arguments onto the stack.
With the SDCC version now in use, a single byte parameter to a `__naked` function is instead passed directly in
register A and never pushed - so the function was reading leftover/garbage stack bytes instead of the intended
row-select value on every single call. This silently broke keyboard input on real hardware (and in MAME): most
keys did nothing, some produced seemingly random/ghosted scancodes, and games like the raycast demo effectively
read noise (which happened to often decode as "turn left"). Fixed by reading the parameter straight from
register A instead of the stack. See `examples/keyboard_test_gl6000sl/` for a small cart that live-displays the
raw matrix state, scancode, keycode and decoded charcode for every key press - useful for verifying keyboard
drivers/mappings on real hardware. `tools/mame_debug/` also has a MAME Lua script for tracing keyboard port I/O.

The test cart was also used to verify the German GL6000SL-specific mappings for `ue` (0x04), `oe` (0x2d) and
`ae` (0x35), as well as the dual-label keys Help/Player 1, Right/Player 2 and Insert/Delete. The driver now
includes those mappings and a preliminary ALT calculator-symbol map based on the red keycap labels. The latter
still needs confirmation on real hardware. `FONT_FULL_ASCII` must be defined before including `vgldk.h` when an
application needs the optional CP437 glyphs used for umlauts and calculator symbols; not all font/display paths
have been verified to render those glyphs yet.

## Getting started
* Clone this repo to some nice place
* Make sure you have SDCC (Small Devices C Compiler) installed
* Make sure you have MAME and "Genius Leader" ROMs installed if you wish to use emulation
* Enter an example directory (e.g. "monitor")
* Look at the `Makefile` and configure the desired target system and configuration flags
* Run `make cart` to compile a cartridge ROM file that can be burned or loaded into MAME
* Run `make emu` to start MAME with the compiled cartridge
* Run `make burn` to burn the cartridge ROM image to an EEPROM using MiniPro
* For speedy development you can connect a 5V TTL USB-to-serial adaptor to the VTech's parallel port (TX to BUSY, RX to DATA) and upload "apps" using `make app` and `make upload` (device must be running the monitor ROM)
* Take a look at the ParallelBuddy Arduino sketch which allows accessing files from SD card or from network

## History
* Started as a stand-alone reverse engineering project in late 2016
* Incorporated the code into the Z88DK repo Development branch (see z88dk.org, platform "vgl")
* Re-visited the whole project using just SDCC in 2019 (see [Hackaday project page](https://hackaday.io/project/166921-v-tech-genius-leader-precomputer-hacking))
* Several sub-projects emerged (CP/M, DOS, serial loader, bus buddy, ...) which I now try to develop under a common development environment
* As of 2020, the VGLDK hardware drivers have become much cleaner "pure C" implementations than the previous Z88DK assembly versions
* CP/M for Genius Leader 4000 series has been completely re-written in 2023-08

## Further Reading
* [Hackaday project page](https://hackaday.io/project/166921-v-tech-genius-leader-precomputer-hacking)
* [Discussion on the Z88DK forum](https://www.z88dk.org/forum/viewtopic.php?id=10055)
* [VTech hardware development](https://www.thingiverse.com/thing:3108809)
* [Alexandre Botzung dissecting the YENO COMPUSAVANT](https://alexandre.botzung.fr/fr/analyse-du-yeno-compusavant)
* [VCXDK for newer VTech models](https://www.github.com/hotkeymuc/vcxdk)
* [VTech on Fandom](https://vtech.fandom.com/wiki/VTech_Wiki)

2023-08-23 Bernhard "HotKey" Slawik
# ESP BASIC Store

An Arduino/ESP sketch that turns a Wemos D1 Mini (or any ESP8266/ESP32, or
even a plain Arduino) into a small external "program store" for
[VGL BASIC](../README.md)'s `CSAVE`/`CLOAD` commands - up to 10 saved
programs, managed from a phone/laptop browser, no PC needed once it's set
up.

**Not yet tested on real hardware** (written without an ESP toolchain or
the physical device on hand) - please treat the timing/wiring notes below
as a starting point and report back what needs adjusting.

## Is it "universal" (ESP or plain Arduino)?

The sketch compiles for all three, via `#if defined(ESP8266)` / `#elif
defined(ESP32)` / `#else`, but honestly: the WiFi/web UI **needs an ESP**
(ESP8266 like the Wemos D1 Mini, or ESP32) - a plain Arduino Uno/Nano/Mega
has no WiFi hardware at all, so that part cannot be "universal" without
adding a WiFi shield or similar. On ESP8266/ESP32 you get the full feature
set: 10 slots as files in flash (LittleFS), web UI, AP+WLAN WiFi. On a
plain AVR Arduino you still get the exact same serial protocol (so it can
still store/recall BASIC programs), just falling back to only 4 small
slots (~250 bytes each) in the built-in EEPROM and no browser interface -
that's a hardware limit (1KB EEPROM on an Uno), not a software choice.

For a Wemos D1 Mini specifically (what was asked for): use the ESP8266
path, full functionality.

## Wiring

Same physical layer as `softuart`/`make upload` (see
`include/arch/gl6000sl/softserial.h` and `../README.md`):

| ESP8266 (Wemos D1 Mini) | VGL parallel port                          |
|-------------------------|---------------------------------------------|
| RX (GPIO3)               | any of pins 2-9 (D0..D7) - **through a voltage divider**, see below |
| TX (GPIO1)               | pin 11 (BUSY), through a ~1k resistor        |
| GND                      | pin 18-25 (GND)                              |

The VGL's parallel port pins are 5V TTL, but the ESP8266/ESP32 are 3.3V and
their inputs are **not 5V tolerant**. Add a simple resistor divider on the
line into the ESP's RX pin (e.g. 10k from the VGL pin to RX, 20k from RX to
GND) to bring it down to a safe ~3.3V. The ESP's 3.3V TX output into the
VGL's BUSY pin is commonly read as a valid logic HIGH by 5V TTL and is what
the existing `softserial.h`/FTDI wiring notes already assume, but this
hasn't been verified for this exact sketch - use a proper level shifter
(e.g. a cheap bidirectional logic-level converter board) instead of the
divider if you want to be safe on both directions.

## Flashing

1. Arduino IDE: install the "ESP8266 boards" package (Board Manager URL
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`) for
   a Wemos D1 Mini, or the "ESP32 boards" package for an ESP32 dev board.
2. Select the board (e.g. "LOLIN(WEMOS) D1 R2 & mini"), a LittleFS-capable
   partition scheme, and the correct COM port.
3. Open `esp_basic_store.ino` and upload.
4. On first boot it starts an access point `VGLBASIC-Setup` (password
   `basic1234`). Connect to it with your phone/laptop and open
   `http://192.168.4.1/` (the ESP8266/ESP32 default AP address).
5. Optional: go to `/wifi` and enter your home WLAN's SSID/password so the
   device *also* joins your existing network (it keeps the `VGLBASIC-Setup`
   AP running too, so you can always fall back to it). Once joined, the
   front page shows its address on your WLAN (and `http://vglbasic.local/`
   should work via mDNS on most OSes/phones).

## Using it from VGL BASIC

Just wire it up and leave it running - the ESP passively listens on serial
for the `CSAVE`/`CLOAD` protocol, no button needs to be pressed on the web
UI at the right moment:

```
10 PRINT "HELLO"
CSAVE 3      REM store the current program into slot 3
NEW
CLOAD 3      REM load it back from slot 3
```

`CSAVE`/`CLOAD` without a number default to slot 0. The protocol is just
two bytes (`S`/`L` + the slot digit `0`-`9`) followed by the same plain
text `CSAVE`/`CLOAD` already used - see `do_csave()`/`do_cload()` in
`../basic.c`.

From the browser you can, per slot: **Edit** (view/edit the raw text and
save it - handy for writing/tweaking a program from a full keyboard),
**Download** (as a `.bas` text file) or **Delete**. There's no "upload
file" button, but pasting a downloaded/edited `.bas` file's contents into
the Edit textarea does the same thing.

## Limitations / notes

* `SLOT_MAX_LEN` (2048 bytes on ESP) matches `PROGRAM_SIZE` in `basic.c` -
  raise both together if you ever grow the BASIC's program buffer.
  `SERIAL_BAUD`/`SERIAL_EOF` must likewise stay in sync with
  `SOFTUART_BAUD`/`SERIAL_EOF` in `basic.c`.
* There's no checksum/retry in this protocol - it's the same trade-off as
  softuart.h itself (no interrupts, no flow control, "poll as often as
  possible"). If the ESP is busy serving a web request for more than
  `SERIAL_RX_TIMEOUT_MS` (5s) while a `CSAVE` is mid-flight, that transfer
  will be aborted/truncated; this hasn't come up in practice yet since
  `server.handleClient()` is called from inside the receive loop too, but
  it's worth knowing about.
* The AP password `basic1234` and hostname are hardcoded - change the
  `#define`s at the top of the `.ino` if you want something else.

#!/bin/bash
# Trace keyboard matrix I/O (ports 0x40-0x43) on GL6000SL in MAME, headless.
#
# Requirements:
#   - MAME installed (this repo's setup used: sudo apt-get install -y mame -> /usr/games/mame)
#   - Your own legally-owned "gl6000sl.zip" romset placed in ~/mame/roms/
#
# Usage:
#   ./run_trace.sh stock                 # trace the original/stock firmware
#   ./run_trace.sh cart <path-to.bin>    # trace a cart image (e.g. our keyboard_test_gl6000sl cart)
#
# Optionally hold one key down for the whole run via natkeyboard (see keyboard_trace.lua):
#   KB_TRACE_KEY=2 ./run_trace.sh cart <path-to.bin>
#
# Output: keyboard_trace.log in the current directory.

set -e

MAME=/usr/games/mame
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECONDS_TO_RUN=${SECONDS_TO_RUN:-15}

# -video none seems to also suppress MAME's per-frame input polling, so held/posted keys never
# reach the emulated ports. Run under a virtual X server (Xvfb) instead so the normal video/input
# frame loop runs, just without a visible window.
RUN="xvfb-run -a"

MODE="$1"

if [ "$MODE" = "stock" ]; then
	$RUN "$MAME" gl6000sl \
		-autoboot_script "$SCRIPT_DIR/keyboard_trace.lua" \
		-sound none -skip_gameinfo \
		-seconds_to_run "$SECONDS_TO_RUN"
elif [ "$MODE" = "cart" ]; then
	CART="$2"
	if [ -z "$CART" ]; then
		echo "Usage: $0 cart <path-to-cart.bin>" >&2
		exit 1
	fi
	$RUN "$MAME" gl6000sl -cart "$CART" \
		-autoboot_script "$SCRIPT_DIR/keyboard_trace.lua" \
		-sound none -skip_gameinfo \
		-seconds_to_run "$SECONDS_TO_RUN"
else
	echo "Usage: $0 stock|cart [path-to-cart.bin]" >&2
	exit 1
fi

echo "Done. See keyboard_trace.log"

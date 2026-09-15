--[[
	MAME autoboot Lua script: trace keyboard matrix I/O on VTech GL6000SL / 7007SL / PreComputer Prestige.

	Logs every OUT/IN on ports 0x40-0x43 (row select + both column read ports, plus one spare byte)
	together with the Z80 program counter at the time, to keyboard_trace.log in the current directory.

	Instead of relying on emu.register_frame_done() + natkeyboard:post() (unreliable with
	-video none - frame_done barely fires without a real video frame being rendered), this directly
	holds one MAME ioport field LOW (pressed) via the ioport API from the very start of the run, so
	there is no timing/frame dependency at all.

	Set which key to hold via environment variables (see run_trace.sh):
		KB_TRACE_PORT  - ioport tag, e.g. "KEY.1"  (matches m_keyboard(*this, "KEY.%u", 0) in MAME's
		                 prestige.cpp - KEY.0..KEY.7 = port 0x41 groups, KEY.8..KEY.15 = port 0x42)
		KB_TRACE_FIELD - field name, e.g. "2"       (matches PORT_NAME(...) in prestige.cpp)

	Usage (headless, no video/sound window):
		KB_TRACE_PORT=KEY.1 KB_TRACE_FIELD=2 /usr/games/mame gl6000sl \
			-cart /path/to/keyboard_test_gl6000sl.cart.8kb.bin \
			-autoboot_script tools/mame_debug/keyboard_trace.lua \
			-video none -sound none -seconds_to_run 15 -skip_gameinfo

	Without -cart, MAME boots the stock firmware from the "gl6000sl" romset instead - useful to
	compare how the ORIGINAL firmware drives ports 0x40-0x43 against our own driver.

	ROM location: put your own legally-owned "gl6000sl.zip" romset in ~/mame/roms/ (MAME's default
	rompath) before running.
]]

local logfile = io.open("keyboard_trace.log", "w")

local function log(fmt, ...)
	local s = string.format(fmt, ...)
	logfile:write(s .. "\n")
	logfile:flush()
end

log("=== keyboard_trace.lua started ===")

local cpu = manager.machine.devices[":maincpu"]
local io_space = cpu.spaces["io"]

local function pc()
	return string.format("%04X", cpu.state["PC"].value)
end

-- NOTE: 'offset' passed to the tap callback is already the ABSOLUTE address (not relative to
-- the tap's start address) - print it directly, do NOT add 0x40 again.
local ok_w, err_w = pcall(function()
	io_space:install_write_tap(0x40, 0x43, "kbd_w_trace", function(offset, data, mask)
		log(string.format("[PC=%s] OUT 0x%02X <- 0x%02X", pc(), offset, data))
	end)
end)
if not ok_w then
	log("install_write_tap failed: " .. tostring(err_w))
end

local ok_r, err_r = pcall(function()
	io_space:install_read_tap(0x40, 0x43, "kbd_r_trace", function(offset, data, mask)
		log(string.format("[PC=%s] IN  0x%02X -> 0x%02X", pc(), offset, data))
	end)
end)
if not ok_r then
	log("install_read_tap failed: " .. tostring(err_r))
end

-- Hold a key down for the whole run using natkeyboard:post_coded() (works once real video frames are
-- being processed, e.g. under Xvfb - "-video none" was found to suppress input polling
-- entirely, so a previous attempt using ioport field:set_value() never took effect either).
local hold_key = os.getenv("KB_TRACE_KEY")
local frame_count = 0
local post_count = 0

if not hold_key then
	log("=== KB_TRACE_KEY not set - no key posted ===")
end

-- Post the key on every frame for the first ~2 seconds of real (post-boot) run time, instead of
-- once at script load (too early - the machine/cart may not even be running yet) or relying on
-- a "queue empty" check (property access on this userdata didn't behave as a plain Lua table).
emu.register_frame_done(function()
	frame_count = frame_count + 1

	if hold_key and frame_count >= 120 and post_count < 120 then
		local ok, err = pcall(function() manager.machine.natkeyboard:post_coded(hold_key) end)
		if ok then
			post_count = post_count + 1
			if post_count == 1 then
				log(string.format("=== started posting coded key '%s' via natkeyboard at frame %d ===", hold_key, frame_count))
			end
		else
			log("natkeyboard post failed: " .. tostring(err))
		end
	end
end)

emu.add_machine_stop_notifier(function()
	log("=== keyboard_trace.lua stopped ===")
	logfile:close()
end)

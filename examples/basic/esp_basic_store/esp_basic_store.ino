/*
	ESP BASIC Store
	===============

	A serial-attached "program store" for VGL BASIC (examples/basic/basic.c).

	Connects to the GL6000SL's parallel port the same way as softuart.h
	(see examples/basic/README.md / include/arch/gl6000sl/softserial.h for
	wiring) and passively listens for the tiny CSAVE/CLOAD protocol:

		'S' '0'-'9'  <text lines...>  0x1A     - CSAVE: store into slot N
		'L' '0'-'9'                            - CLOAD: send slot N back,
		                                          <text lines...> 0x1A

	i.e. exactly what basic.c's do_csave()/do_cload() already send/expect -
	no changes needed on the VGL side beyond the slot-number support that
	was added alongside this sketch.

	On ESP8266/ESP32: 10 slots are kept as files in LittleFS (persists
	across power loss) and a small web UI lets you view/download/upload/
	delete each slot from a browser. WiFi starts in AP mode (so you can
	always reach it even with no existing WiFi around) and additionally
	joins your WLAN as a station if credentials were saved via the "/wifi"
	page - both at the same time (WIFI_AP_STA).

	On a plain AVR Arduino (Uno/Nano/Mega, no WiFi hardware): the sketch
	still compiles and still speaks the same serial protocol, but falls
	back to a much smaller EEPROM-backed store (a handful of short
	programs) and has no web UI - there is no way around that without
	extra hardware (WiFi shield/SD card), see README.md.

	NOT tested on real hardware yet (no ESP8266/ESP32 toolchain available
	in the dev sandbox this was written in) - please report back if the
	serial timing/level-shifting notes in the README need adjusting.

	2026-09-22 Bernhard "HotKey" Slawik
*/

#define SERIAL_BAUD 9600	// Must match SOFTUART_BAUD in examples/basic/basic.c
#define SERIAL_EOF 0x1a		// Must match SERIAL_EOF in examples/basic/basic.c
#define NUM_SLOTS 10

#if defined(ESP8266)
	#include <ESP8266WiFi.h>
	#include <ESP8266WebServer.h>
	#include <ESP8266mDNS.h>
	#include <LittleFS.h>
	#define WebServerClass ESP8266WebServer
	#define WIFI_CAPABLE 1
#elif defined(ESP32)
	#include <WiFi.h>
	#include <WebServer.h>
	#include <ESPmDNS.h>
	#include <LittleFS.h>
	#define WebServerClass WebServer
	#define WIFI_CAPABLE 1
#else
	// Plain AVR (Uno/Nano/Mega/...): no WiFi hardware, no filesystem.
	#include <EEPROM.h>
	#define WIFI_CAPABLE 0
#endif

#if WIFI_CAPABLE

#define SLOT_MAX_LEN 2048	// matches PROGRAM_SIZE in basic.c
#define AP_SSID "VGLBASIC-Setup"
#define AP_PASS "basic1234"	// must be >=8 chars for a WPA2 AP
#define WIFI_CREDS_FILE "/wifi.txt"
#define SERIAL_RX_TIMEOUT_MS 5000	// abort a stuck CSAVE if the VGL goes quiet mid-transfer

WebServerClass server(80);

String slotPath(int n) {
	return "/slot" + String(n) + ".bas";
}

size_t slotSize(int n) {
	File f = LittleFS.open(slotPath(n), "r");
	if (!f) return 0;
	size_t s = f.size();
	f.close();
	return s;
}

// ---- WiFi ------------------------------------------------------------

void loadAndConnectWifi() {
	File f = LittleFS.open(WIFI_CREDS_FILE, "r");
	if (!f) return;
	String ssid = f.readStringUntil('\n');
	String pass = f.readStringUntil('\n');
	f.close();
	ssid.trim();
	pass.trim();
	if (ssid.length() == 0) return;
	WiFi.begin(ssid.c_str(), pass.c_str());
	Serial.println("Connecting to WLAN: " + ssid);
}

void saveWifiCreds(const String &ssid, const String &pass) {
	File f = LittleFS.open(WIFI_CREDS_FILE, "w");
	if (!f) return;
	f.println(ssid);
	f.println(pass);
	f.close();
}

// ---- Web UI ------------------------------------------------------------

String htmlHeader(const String &title) {
	String h = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
	h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
	h += "<title>" + title + "</title></head><body style='font-family:sans-serif'>";
	h += "<h2>" + title + "</h2>";
	return h;
}

void handleRoot() {
	String h = htmlHeader("VGL BASIC Store");

	h += "<p>AP: <b>" AP_SSID "</b> (" + WiFi.softAPIP().toString() + ")";
	if (WiFi.status() == WL_CONNECTED) {
		h += " | WLAN: <b>" + WiFi.SSID() + "</b> (" + WiFi.localIP().toString() + ")";
	} else {
		h += " | WLAN: not connected (<a href='/wifi'>set up</a>)";
	}
	h += "</p><table border='1' cellpadding='6' style='border-collapse:collapse'>";
	h += "<tr><th>Slot</th><th>Size</th><th>Actions</th></tr>";
	for (int i = 0; i < NUM_SLOTS; i++) {
		size_t sz = slotSize(i);
		h += "<tr><td>" + String(i) + "</td><td>";
		h += (sz > 0) ? (String(sz) + " bytes") : "empty";
		h += "</td><td>";
		h += "<a href='/slot?n=" + String(i) + "'>Edit</a> | ";
		h += "<a href='/slot?n=" + String(i) + "&download=1'>Download</a> | ";
		h += "<a href='/delete?n=" + String(i) + "' onclick=\"return confirm('Delete slot " + String(i) + "?')\">Delete</a>";
		h += "</td></tr>";
	}
	h += "</table>";
	h += "<p>Serial protocol (from VGL BASIC): <code>CSAVE n</code> stores the "
	     "current program into slot n, <code>CLOAD n</code> sends it back. "
	     "n defaults to 0 if omitted.</p>";
	h += "</body></html>";
	server.send(200, "text/html", h);
}

void handleSlot() {
	if (!server.hasArg("n")) { server.send(400, "text/plain", "missing n"); return; }
	int n = server.arg("n").toInt();
	if (n < 0 || n >= NUM_SLOTS) { server.send(400, "text/plain", "bad slot"); return; }

	if (server.method() == HTTP_POST) {
		File f = LittleFS.open(slotPath(n), "w");
		if (f) {
			String body = server.arg("content");
			f.print(body);
			f.close();
		}
		server.sendHeader("Location", "/slot?n=" + String(n));
		server.send(303);
		return;
	}

	File f = LittleFS.open(slotPath(n), "r");
	String content = "";
	if (f) {
		content = f.readString();
		f.close();
	}

	if (server.hasArg("download")) {
		server.sendHeader("Content-Disposition", "attachment; filename=slot" + String(n) + ".bas");
		server.send(200, "text/plain", content);
		return;
	}

	String h = htmlHeader("Slot " + String(n));
	h += "<form method='POST' action='/slot?n=" + String(n) + "'>";
	h += "<textarea name='content' rows='20' cols='60' style='font-family:monospace'>";
	// Content is stored as plain text (same format as LIST); escape for HTML display
	for (size_t i = 0; i < content.length(); i++) {
		char c = content[i];
		if (c == '&') h += "&amp;";
		else if (c == '<') h += "&lt;";
		else if (c == '>') h += "&gt;";
		else h += c;
	}
	h += "</textarea><br><input type='submit' value='Save'></form>";
	h += "<p><a href='/'>&laquo; back</a></p></body></html>";
	server.send(200, "text/html", h);
}

void handleDelete() {
	if (server.hasArg("n")) {
		int n = server.arg("n").toInt();
		if (n >= 0 && n < NUM_SLOTS) LittleFS.remove(slotPath(n));
	}
	server.sendHeader("Location", "/");
	server.send(303);
}

void handleWifiForm() {
	String h = htmlHeader("WiFi setup");
	h += "<form method='POST' action='/wifi'>";
	h += "SSID: <input name='ssid'><br>";
	h += "Password: <input name='pass' type='password'><br>";
	h += "<input type='submit' value='Connect'></form>";
	h += "<p>Joins your WLAN in addition to keeping the <b>" AP_SSID "</b> "
	     "access point active, so this page stays reachable either way.</p>";
	h += "<p><a href='/'>&laquo; back</a></p></body></html>";
	server.send(200, "text/html", h);
}

void handleWifiSave() {
	String ssid = server.arg("ssid");
	String pass = server.arg("pass");
	saveWifiCreds(ssid, pass);
	WiFi.begin(ssid.c_str(), pass.c_str());
	server.sendHeader("Location", "/");
	server.send(303);
}

void setupWeb() {
	server.on("/", handleRoot);
	server.on("/slot", handleSlot);
	server.on("/delete", handleDelete);
	server.on("/wifi", HTTP_GET, handleWifiForm);
	server.on("/wifi", HTTP_POST, handleWifiSave);
	server.begin();
}

// ---- Serial protocol (CSAVE/CLOAD) -------------------------------------

// Waits (briefly) for the slot-number digit that follows 'S'/'L'. Returns
// 0-9, or -1 if nothing sensible arrived (malformed/noise on the line).
int readSlotDigit() {
	unsigned long start = millis();
	while (millis() - start < 500) {
		if (Serial.available()) {
			int c = Serial.read();
			if (c >= '0' && c <= '9') return c - '0';
			return -1;
		}
	}
	return -1;
}

void receiveProgramToSlot(int n) {
	File f = LittleFS.open(slotPath(n), "w");
	unsigned long lastByte = millis();
	size_t written = 0;
	for (;;) {
		if (Serial.available()) {
			int c = Serial.read();
			lastByte = millis();
			if (c == SERIAL_EOF) break;
			if (f && written < SLOT_MAX_LEN) { f.write((uint8_t)c); written++; }
		} else {
			if (millis() - lastByte > SERIAL_RX_TIMEOUT_MS) break;	// VGL went quiet - give up
			server.handleClient();	// keep the web UI responsive while waiting
		}
	}
	if (f) f.close();
}

void sendProgramFromSlot(int n) {
	File f = LittleFS.open(slotPath(n), "r");
	if (f) {
		while (f.available()) Serial.write((uint8_t)f.read());
		f.close();
	}
	Serial.write((uint8_t)SERIAL_EOF);
}

void handleSerialProtocol() {
	if (!Serial.available()) return;
	int cmd = Serial.read();
	if (cmd != 'S' && cmd != 'L') return;	// ignore stray/noise bytes
	int slot = readSlotDigit();
	if (slot < 0) return;
	if (cmd == 'S') receiveProgramToSlot(slot);
	else sendProgramFromSlot(slot);
}

void setup() {
	Serial.begin(SERIAL_BAUD);
	LittleFS.begin();

	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(AP_SSID, AP_PASS);
	loadAndConnectWifi();

	MDNS.begin("vglbasic");	// reachable at http://vglbasic.local/ once on the same WLAN
	setupWeb();
}

void loop() {
	server.handleClient();
	handleSerialProtocol();
}

#else	// !WIFI_CAPABLE - plain AVR Arduino fallback: no WiFi, tiny EEPROM store

// Small enough to fit an ATmega328 (Uno/Nano, 1KB EEPROM): 4 slots of up
// to ~250 bytes each (2-byte length prefix + data). No web UI possible -
// there's no WiFi hardware on these boards. See README.md.
#define NUM_SLOTS_AVR 4
#define SLOT_MAX_LEN_AVR 250
#define SLOT_STRIDE (2 + SLOT_MAX_LEN_AVR)

int slotAddr(int n) { return n * SLOT_STRIDE; }

int readSlotDigit() {
	unsigned long start = millis();
	while (millis() - start < 500) {
		if (Serial.available()) {
			int c = Serial.read();
			if (c >= '0' && c <= '9') return c - '0';
			return -1;
		}
	}
	return -1;
}

void receiveProgramToSlot(int n) {
	if (n >= NUM_SLOTS_AVR) n = NUM_SLOTS_AVR - 1;
	byte buf[SLOT_MAX_LEN_AVR];
	unsigned int len = 0;
	unsigned long lastByte = millis();
	for (;;) {
		if (Serial.available()) {
			int c = Serial.read();
			lastByte = millis();
			if (c == SERIAL_EOF) break;
			if (len < SLOT_MAX_LEN_AVR) buf[len++] = (byte)c;
		} else {
			if (millis() - lastByte > 5000) break;
		}
	}
	int addr = slotAddr(n);
	EEPROM.put(addr, len);
	for (unsigned int i = 0; i < len; i++) EEPROM.update(addr + 2 + i, buf[i]);
}

void sendProgramFromSlot(int n) {
	if (n >= NUM_SLOTS_AVR) n = NUM_SLOTS_AVR - 1;
	int addr = slotAddr(n);
	unsigned int len = 0;
	EEPROM.get(addr, len);
	if (len > SLOT_MAX_LEN_AVR) len = 0;	// uninitialized EEPROM guard
	for (unsigned int i = 0; i < len; i++) Serial.write(EEPROM.read(addr + 2 + i));
	Serial.write((uint8_t)SERIAL_EOF);
}

void handleSerialProtocol() {
	if (!Serial.available()) return;
	int cmd = Serial.read();
	if (cmd != 'S' && cmd != 'L') return;
	int slot = readSlotDigit();
	if (slot < 0) return;
	if (cmd == 'S') receiveProgramToSlot(slot);
	else sendProgramFromSlot(slot);
}

void setup() {
	Serial.begin(SERIAL_BAUD);
}

void loop() {
	handleSerialProtocol();
}

#endif

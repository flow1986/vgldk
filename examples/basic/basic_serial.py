#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Host-side helper for VGL BASIC's CSAVE/CLOAD.

CSAVE sends the stored program as plain text lines over the parallel port
(bit-banged softuart, see include/driver/softuart.h), terminated by a single
0x1A (EOF) byte. CLOAD just reads text lines back in the same format - so
receiving/sending a program from a PC is nothing more than logging/replaying
that byte stream through a plain serial adapter.

Wiring (see include/arch/gl6000sl/softserial.h for the full pinout):
	USB-serial adapter (5V/3.3V TTL, e.g. FTDI/CP2102)   VGL parallel port
	---------------------------------------------------  --------------------------------
	RXD  <-------------------------------------------->   any of pins 2-9 (D0..D7)
	TXD  --[1k resistor]------------------------------>   pin 11 (BUSY)
	GND  <-------------------------------------------->   pin 18-25 (GND)

Usage:
	python3 basic_serial.py receive myprogram.bas   # run CSAVE on the VGL, then start this
	python3 basic_serial.py send myprogram.bas      # run CLOAD on the VGL, then start this

Requires: pip install pyserial

2026-09-22 Bernhard "HotKey" Slawik
"""

import sys
import time
import serial

PORT = '/dev/ttyUSB0'
BAUD = 9600		# Must match SOFTUART_BAUD in basic.c
EOF_BYTE = 0x1a	# Matches SERIAL_EOF in basic.c

# Delay between sent bytes: the VGL side is a busy-wait bit-banged UART with
# no buffering, so sending too fast can drop bytes. Untested/untuned - raise
# this if CLOAD garbles/misses lines on real hardware.
SEND_DELAY = 0.01


def receive(path):
	ser = serial.Serial(PORT, BAUD, timeout=1)
	print('Waiting for data... (run CSAVE on the VGL now, Ctrl+C to abort)')
	data = bytearray()
	try:
		while True:
			b = ser.read(1)
			if not b:
				continue
			if b[0] == EOF_BYTE:
				break
			data += b
			sys.stdout.write(chr(b[0]))
			sys.stdout.flush()
	except KeyboardInterrupt:
		print('\nAborted.')
		return
	with open(path, 'wb') as f:
		f.write(data)
	print('\nSaved %d bytes to %s' % (len(data), path))


def send(path):
	with open(path, 'rb') as f:
		data = f.read()
	ser = serial.Serial(PORT, BAUD, timeout=1)
	print('Run CLOAD on the VGL now, then press Enter here...')
	input()
	for b in data:
		ser.write(bytes([b]))
		time.sleep(SEND_DELAY)
	ser.write(bytes([EOF_BYTE]))
	print('Sent %d bytes' % len(data))


def main():
	if len(sys.argv) != 3 or sys.argv[1] not in ('send', 'receive'):
		print('Usage: %s send|receive <file>' % sys.argv[0])
		sys.exit(1)
	if sys.argv[1] == 'send':
		send(sys.argv[2])
	else:
		receive(sys.argv[2])


if __name__ == '__main__':
	main()

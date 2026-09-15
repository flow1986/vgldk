/*
	Keyboard scancode/keycode tester for the VTech Genius LEADER 6000SL / 7007SL / PreComputer Prestige
	
	Continuously scans the raw matrix (ports 0x40/0x41/0x42) every frame and shows:
	
		* The raw electrical matrix state (which row/col intersections are pulled LOW)
		* The resulting SCANCODE (0x00-0x3f = matrix 1 @ port 0x41, 0x40-0x7f = matrix 2 @ port 0x42)
		* The KEYCODE (from KEY_CODES[]) and the fully decoded charcode (shift/symbol/alt applied,
		  via keyboard_inkey()) for comparison
	
	Press any key and compare what's shown here to the letter printed on the keycap. If it doesn't
	match, KEY_CODES[]/KEY_MAP_SHIFT[] (include/arch/gl6000sl/keyboard.h) needs to be fixed for that
	scancode.
	
	2026 HotKey
*/
// Needed so the LCD font actually has glyphs for the German umlauts (CP437 codes 0x80-0xff),
// otherwise keycode/charcode values above 0x7f always render as a blank/unknown glyph.
#define FONT_FULL_ASCII

#include <vgldk.h>
#include <stdiomin.h>
#include <hex.h>	// for printf_x2()

// Must match the working examples (e.g. raycast, hello): a "cart" is entered via `jp _main`
// (not `call`), so main() must take no parameters and not expect a return address on the stack.
void main() {
	byte my, mx;
	byte b1, b2;
	byte scancode;
	byte keycode;
	byte c;
	
	while (1) {
		// Redraw from top-left every frame (fixed layout, no scrolling)
		lcd_text_col = 0;
		lcd_text_row = 0;
		
		printf("GL6000SL Keyboard Test\n");
		printf("row IN1:scan00-07 IN2:scan40-47\n");
		
		scancode = 0xff;
		
		for (my = 0; my < 8; my++) {
			// Activate only row 'my' (active LOW)
			keyboard_matrix_out(0xff - (1 << my));
			
			b1 = keyboard_matrix_in1();
			b2 = keyboard_matrix_in2();
			
			printf_x2(my);
			putchar(':');
			putchar(' ');
			for (mx = 0; mx < 8; mx++) {
				if ((b1 & (1 << mx)) == 0) {
					putchar('X');
					scancode = my * 8 + mx;
				} else {
					putchar('.');
				}
			}
			putchar(' ');
			putchar(' ');
			for (mx = 0; mx < 8; mx++) {
				if ((b2 & (1 << mx)) == 0) {
					putchar('X');
					scancode = 0x40 + my * 8 + mx;
				} else {
					putchar('.');
				}
			}
			putchar('\n');
		}
		
		// Deactivate matrix (idle / all rows high)
		keyboard_matrix_out(0xff);
		
		putchar('\n');
		
		if (scancode != 0xff) {
			printf("scancode: 0x");
			printf_x2(scancode);
			
			keycode = KEY_CODES[scancode];
			
			printf("  keycode: 0x");
			printf_x2(keycode);
			
			putchar(' ');
			putchar('\'');
			if ((keycode >= 0x20) && (keycode != 0x7f)) {
				putchar(keycode);
			} else {
				putchar('?');
			}
			putchar('\'');
			printf("          \n");
		} else {
			printf("scancode: --   (no key pressed)          \n");
		}
		
		// Also show the fully decoded charcode (with shift/symbol/alt applied) via the normal
		// keyboard driver, for comparison against the raw scancode/keycode above.
		c = keyboard_inkey();
		if (c != KEY_CHARCODE_NONE) {
			printf("charcode: 0x");
			printf_x2(c);
			putchar(' ');
			putchar('\'');
			if ((c >= 0x20) && (c != 0x7f)) {
				putchar(c);
			} else {
				putchar('?');
			}
			putchar('\'');
			printf("          \n");
		} else {
			printf("charcode: --                  \n");
		}
	}
}

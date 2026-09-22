/*
	A small line-numbered BASIC interpreter for the VGLDK (GL6000SL)

	Classic 1980s home-computer style BASIC: type a line starting with a
	number to store it in the program, type a line without a number to
	execute it immediately. The whole program is kept as plain text
	(line number + text per line, sorted by line number) directly in RAM -
	no tokenizer, statements are re-parsed every time they run.

	Supported:
		LET (optional), PRINT, INPUT,
		IF ... THEN <line> [ELSE <line>] | IF ... THEN <stmt>,
		GOTO, GOSUB/RETURN, FOR/TO/STEP/NEXT, REM, END/STOP,
		PEEK(addr)/POKE addr,val, PLOT x,y[,c], LINE x0,y0,x1,y1,
		DEFSP n,row,hex, DRASP n,x,y[,c], MOVSP n,x,y,
		SOUND freq,len, BEEP, CLS, RND(n), ABS(n)
		LIST, RUN, NEW, EDIT n, SAVE, LOAD (immediate commands)
	Variables: 26 integers A..Z (single uppercase letter).
	Numbers: signed 16 bit ints. No strings/arrays (keeps it small+simple).

	The line editor (used for both program lines and immediate commands)
	supports LEFT/RIGHT arrow cursor movement, INSERT/overwrite-free typing
	and a blinking cursor block, all done by polling keyboard_inkey() in a
	busy-wait loop (no hardware timer on this platform, see input_line()).

	SAVE/LOAD keep one extra full copy of the program in a second RAM
	buffer ("im internen RAM") - a lightweight undo/backup slot, not a
	real EEPROM/filesystem save (there is no verified persistent storage
	driver for this hardware yet).

	CSAVE/CLOAD stream the program as plain text over the parallel port
	(bit-banged UART via driver/softuart.h, same physical layer already
	proven on GL6000SL/GL4000 by examples/monitor + examples/cpm) - an
	Arduino/ESP wired to it (see README.md) can log/replay that text to
	act as an external "cassette"/program store. No special protocol:
	it's just the same text you'd type at the prompt, one line at a time,
	terminated with a 0x1A (EOF) byte.
*/

#include <vgldk.h>
#include <stdiomin.h>

#define SOFTUART_SERIES 6000
#define SOFTUART_BAUD 9600	// GL6000SL: 9600 is the reliable/proven rate (19200 is experimental)
#include <driver/softuart.h>


// ---- Program storage (kept in plain internal RAM) -----------------------

#define PROGRAM_SIZE 2048	// bytes of RAM reserved for the stored program
byte program[PROGRAM_SIZE];
word program_len;	// program[0..program_len-1] holds all stored lines;
					// program[program_len] / [program_len+1] are always 0x00 (end marker)

byte saved_program[PROGRAM_SIZE];	// backup slot used by SAVE/LOAD
word saved_program_len;
byte has_saved;

#define SPRITE_COUNT 8
#define SPRITE_ROWS 8
byte sprite_data[SPRITE_COUNT][SPRITE_ROWS];
byte sprite_x[SPRITE_COUNT];
byte sprite_y[SPRITE_COUNT];

int vars[26];	// variables A..Z

byte running;		// RUN in progress?
byte *line_start;	// pointer to the lineno-word of the line currently executing
word cur_line;		// line number currently executing (for error messages)
byte jump_flag;		// set by GOTO/GOSUB/RETURN/NEXT to redirect the run loop
byte *jump_target;	// where to jump to (points at a line's lineno-word)

word rng_state = 1;
byte key_last;

// FOR/NEXT stack
#define FOR_STACK_MAX 6
typedef struct {
	byte var;
	int cur;
	int limit;
	int step;
	byte *body_start;
} t_for_frame;
t_for_frame for_stack[FOR_STACK_MAX];
byte for_sp;

// GOSUB stack
#define GOSUB_STACK_MAX 8
byte *gosub_stack[GOSUB_STACK_MAX];
byte gosub_sp;


// ---- Forward declarations ------------------------------------------------

int parse_expr(char **pp);
void exec_statement(char **pp);
void print_int_signed(int v);
void handle_input_line(char *line);
void delay_nop(word n);

byte hex_digit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return 0;
}

byte parse_hex_byte(char **pp) {
	char *p = *pp;
	byte value = 0;
	byte digits = 0;
	byte binary = 1;
	byte i;

	for (i = 0; i < 8; i++) {
		if (p[i] != '0' && p[i] != '1') { binary = 0; break; }
	}
	if (binary) {
		for (i = 0; i < 8; i++) value = (value << 1) | (p[i] - '0');
		*pp = p + 8;
		return value;
	}

	if (p[0] == '$') p++;
	else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
	while (digits < 2 && ((p[0] >= '0' && p[0] <= '9') ||
		(p[0] >= 'A' && p[0] <= 'F') || (p[0] >= 'a' && p[0] <= 'f'))) {
		value = (value << 4) | hex_digit(*p++);
		digits++;
	}
	*pp = p;
	return value;
}

byte key_current() {
	byte key;

	key = keyboard_inkey();
	if (key == 204) key = '<';
	if (key == 206) key = '>';
	if (key != KEY_CHARCODE_NONE) {
		key_last = key;
	} else if (!keyboard_ispressed()) {
		key_last = KEY_CHARCODE_NONE;
	}
	return key_last;
}

byte basic_keyboard_inkey() {
	byte key = keyboard_inkey();
	if (key == 204) return '<';
	if (key == 206) return '>';
	return key;
}

byte check_break() {
	keyboard_update();
	if (keyboard_buffer_in != keyboard_buffer_out &&
		keyboard_buffer[keyboard_buffer_out] == KEY_ESCAPE) {
		keyboard_buffer_out = (keyboard_buffer_out + 1) % KEYBOARD_BUFFER_MAX;
		return 1;
	}
	return 0;
}


// ---- Small helpers (no string.h on this toolchain) -----------------------

word strlen_local(char *s) {
	word n = 0;
	while (s[n]) n++;
	return n;
}

void memmove_local(byte *dst, byte *src, word len) {
	word i;
	if (dst == src || len == 0) return;
	if (dst < src) {
		for (i = 0; i < len; i++) dst[i] = src[i];
	} else {
		for (i = len; i > 0; i--) dst[i-1] = src[i-1];
	}
}

void skip_spaces(char **pp) {
	char *p = *pp;
	while (*p == ' ' || *p == '\t') p++;
	*pp = p;
}

// Case-insensitive keyword match. Advances *pp past the keyword (and only
// the keyword) if it matches AND is not followed by another letter/digit
// (so "GOTO" doesn't accidentally match the start of "GOTOX").
byte match_keyword(char **pp, char *kw) {
	char *p = *pp;
	char c, k;
	byte i = 0;
	for (;;) {
		k = kw[i];
		if (k == 0) break;
		c = p[i];
		if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
		if (c != k) return false;
		i++;
	}
	c = p[i];
	if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return false;
	*pp = p + i;
	return true;
}

void error(char *msg) {
	printf("ERR ");
	printf(msg);
	putchar(' ');
	printf("LINE ");
	print_int_signed((int)cur_line);
	putchar('\n');
	running = 0;
}


// ---- Number printing ------------------------------------------------------

void print_int_signed(int v) {
	char buf[7];
	byte i = 0;
	word uv;
	if (v < 0) {
		putchar('-');
		uv = (word)(-v);
	} else {
		uv = (word)v;
	}
	if (uv == 0) {
		putchar('0');
		return;
	}
	while (uv > 0) {
		buf[i++] = '0' + (uv % 10);
		uv /= 10;
	}
	while (i > 0) putchar(buf[--i]);
}


// ---- Pseudo random number generator ---------------------------------------

word next_rand() {
	rng_state = rng_state * 25173 + 13849;
	return rng_state;
}


// ---- Framebuffer drawing (PLOT/LINE) ---------------------------------------

void plot_pixel(int x, int y, byte color) {
	byte *p;
	byte mask;
	if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
	p = (byte *)(lcd_addr + (word)y * LCD_SCANLINE_SIZE + (word)(x >> 3));
	mask = 0x80 >> (x & 7);
	if (color) *p |= mask;
	else *p &= ~mask;
}

void draw_line(int x0, int y0, int x1, int y1, byte color) {
	int dx, dy, sx, sy, err, e2;
	dx = x1 - x0; if (dx < 0) dx = -dx;
	dy = y1 - y0; if (dy < 0) dy = -dy;
	sx = (x0 < x1) ? 1 : -1;
	sy = (y0 < y1) ? 1 : -1;
	err = dx - dy;
	for (;;) {
		plot_pixel(x0, y0, color);
		if (x0 == x1 && y0 == y1) break;
		e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}
}

void draw_sprite(byte number, int x, int y, byte color) {
	byte row, col;
	byte bits;
	if (number >= SPRITE_COUNT) return;
	for (row = 0; row < SPRITE_ROWS; row++) {
		bits = sprite_data[number][row];
		for (col = 0; col < 8; col++) {
			if (bits & (0x80 >> col)) plot_pixel(x + col, y + row, color);
		}
	}
}

void do_defsprite(char **pp) {
	char *p = *pp;
	int number, row;
	skip_spaces(&p);
	number = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	row = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	skip_spaces(&p);
	if (number >= 0 && number < SPRITE_COUNT && row >= 0 && row < SPRITE_ROWS)
		sprite_data[number][row] = parse_hex_byte(&p);
	else
		parse_hex_byte(&p);
	*pp = p;
}

void do_drawsprite(char **pp) {
	char *p = *pp;
	int number, x, y, color = 1;
	number = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	x = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	y = parse_expr(&p);
	skip_spaces(&p);
	if (*p == ',') { p++; color = parse_expr(&p); }
	draw_sprite((byte)number, x, y, (byte)color);
	if (number >= 0 && number < SPRITE_COUNT) {
		sprite_x[number] = (byte)x;
		sprite_y[number] = (byte)y;
	}
	*pp = p;
}

void do_movesprite(char **pp) {
	char *p = *pp;
	int number, x, y;
	number = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	x = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	y = parse_expr(&p);
	if (number >= 0 && number < SPRITE_COUNT) {
		draw_sprite((byte)number, sprite_x[number], sprite_y[number], 0);
		sprite_x[number] = (byte)x;
		sprite_y[number] = (byte)y;
		draw_sprite((byte)number, x, y, 1);
	}
	*pp = p;
}


// ---- Expression parser ------------------------------------------------
// Grammar (top to bottom = lowest to highest precedence):
//   expr    := compare
//   compare := addsub ( (= <> < > <= >=) addsub )?
//   addsub  := muldiv ( (+ -) muldiv )*
//   muldiv  := unary ( (* / MOD) unary )*
//   unary   := ('-'|'+')? primary
//   primary := NUMBER | VAR | '(' expr ')' | PEEK(expr) | RND(expr) | ABS(expr)

int parse_primary(char **pp) {
	char *p = *pp;
	int v;
	skip_spaces(&p);
	if (*p == '(') {
		p++;
		v = parse_expr(&p);
		skip_spaces(&p);
		if (*p == ')') p++;
		*pp = p;
		return v;
	}
	if (match_keyword(&p, "PEEK")) {
		skip_spaces(&p);
		if (*p == '(') p++;
		v = parse_expr(&p);
		skip_spaces(&p);
		if (*p == ')') p++;
		*pp = p;
		return (int)(*(byte *)(word)v);
	}
	if (match_keyword(&p, "RND")) {
		skip_spaces(&p);
		if (*p == '(') p++;
		v = parse_expr(&p);
		skip_spaces(&p);
		if (*p == ')') p++;
		*pp = p;
		if (v <= 0) return 0;
		return (int)(next_rand() % (word)v);
	}
	if (match_keyword(&p, "ABS")) {
		skip_spaces(&p);
		if (*p == '(') p++;
		v = parse_expr(&p);
		skip_spaces(&p);
		if (*p == ')') p++;
		*pp = p;
		return (v < 0) ? -v : v;
	}
	if (match_keyword(&p, "SPX") || match_keyword(&p, "SPRITEX")) {
		skip_spaces(&p);
		if (*p == '(') p++;
		v = parse_expr(&p);
		skip_spaces(&p);
		if (*p == ')') p++;
		*pp = p;
		if (v < 0 || v >= SPRITE_COUNT) return 0;
		return sprite_x[v];
	}
	if (match_keyword(&p, "SPY") || match_keyword(&p, "SPRITEY")) {
		skip_spaces(&p);
		if (*p == '(') p++;
		v = parse_expr(&p);
		skip_spaces(&p);
		if (*p == ')') p++;
		*pp = p;
		if (v < 0 || v >= SPRITE_COUNT) return 0;
		return sprite_y[v];
	}
	if (match_keyword(&p, "KEY")) {
		// Non-blocking key read for games: 0 if nothing is currently
		// pressed, else the charcode of the key - unlike INPUT/gets()
		// this never waits. Optional empty parens are allowed: KEY()
		skip_spaces(&p);
		if (*p == '(') {
			p++;
			skip_spaces(&p);
			if (*p == ')') p++;
		}
		*pp = p;
		return (int)key_current();
	}
	if (*p >= '0' && *p <= '9') {
		v = 0;
		while (*p >= '0' && *p <= '9') {
			v = v * 10 + (*p - '0');
			p++;
		}
		*pp = p;
		return v;
	}
	if (*p >= 'A' && *p <= 'Z') {
		v = vars[*p - 'A'];
		p++;
		*pp = p;
		return v;
	}
	// Unknown token: don't loop forever, just consume it
	if (*p) p++;
	*pp = p;
	return 0;
}

int parse_unary(char **pp) {
	char *p = *pp;
	int v;
	skip_spaces(&p);
	if (*p == '-') {
		p++;
		v = -parse_unary(&p);
		*pp = p;
		return v;
	}
	if (*p == '+') {
		p++;
		v = parse_unary(&p);
		*pp = p;
		return v;
	}
	v = parse_primary(&p);
	*pp = p;
	return v;
}

int parse_muldiv(char **pp) {
	char *p = *pp;
	int v, v2;
	v = parse_unary(&p);
	for (;;) {
		skip_spaces(&p);
		if (*p == '*') {
			p++;
			v2 = parse_unary(&p);
			v *= v2;
		} else if (*p == '/') {
			p++;
			v2 = parse_unary(&p);
			v = (v2 != 0) ? (v / v2) : 0;
		} else if (match_keyword(&p, "MOD")) {
			v2 = parse_unary(&p);
			v = (v2 != 0) ? (v % v2) : 0;
		} else {
			break;
		}
	}
	*pp = p;
	return v;
}

int parse_addsub(char **pp) {
	char *p = *pp;
	int v, v2;
	v = parse_muldiv(&p);
	for (;;) {
		skip_spaces(&p);
		if (*p == '+') {
			p++;
			v2 = parse_muldiv(&p);
			v += v2;
		} else if (*p == '-') {
			p++;
			v2 = parse_muldiv(&p);
			v -= v2;
		} else {
			break;
		}
	}
	*pp = p;
	return v;
}

int parse_compare(char **pp) {
	char *p = *pp;
	int v, v2;
	v = parse_addsub(&p);
	skip_spaces(&p);
	if (p[0] == '<' && p[1] == '>') { p += 2; v2 = parse_addsub(&p); v = (v != v2); }
	else if (p[0] == '<' && p[1] == '=') { p += 2; v2 = parse_addsub(&p); v = (v <= v2); }
	else if (p[0] == '>' && p[1] == '=') { p += 2; v2 = parse_addsub(&p); v = (v >= v2); }
	else if (p[0] == '<') { p += 1; v2 = parse_addsub(&p); v = (v < v2); }
	else if (p[0] == '>') { p += 1; v2 = parse_addsub(&p); v = (v > v2); }
	else if (p[0] == '=') { p += 1; v2 = parse_addsub(&p); v = (v == v2); }
	*pp = p;
	return v;
}

int parse_expr(char **pp) {
	return parse_compare(pp);
}


// ---- Program storage: store/find lines -------------------------------

word entry_len(byte *p) {
	return 2 + strlen_local((char *)(p + 2)) + 1;
}

byte *find_line_exact(word ln) {
	byte *p = program;
	word cur;
	while (p < program + program_len) {
		cur = p[0] | (p[1] << 8);
		if (cur == ln) return p;
		p += entry_len(p);
	}
	return NULL;
}

byte *next_line_ptr() {
	return line_start + entry_len(line_start);
}

char hex_char(byte value) {
	return (value < 10) ? ('0' + value) : ('A' + value - 10);
}

void normalize_defsprite(char *text) {
	char *p = text;
	byte value;
	byte i;

	skip_spaces(&p);
	if (!match_keyword(&p, "DEFSP")) return;
	while (*p && *p != ',') p++;
	if (*p == 0) return;
	p++;
	while (*p && *p != ',') p++;
	if (*p == 0) return;
	p++;
	skip_spaces(&p);
	for (i = 0; i < 8; i++) {
		if (p[i] != '0' && p[i] != '1') return;
	}
	value = 0;
	for (i = 0; i < 8; i++) value = (value << 1) | (p[i] - '0');
	p[0] = hex_char(value >> 4);
	p[1] = hex_char(value & 0x0f);
	memmove_local(p + 2, p + 8, strlen_local(p + 8) + 1);
}

void store_line(word ln, char *text) {
	byte *p = program;
	word cur;
	byte *old_pos = NULL;
	word old_len = 0;
	word text_len = strlen_local(text);
	word new_len = 2 + text_len + 1;

	while (p < program + program_len) {
		cur = p[0] | (p[1] << 8);
		if (cur == ln) { old_pos = p; old_len = entry_len(p); break; }
		if (cur > ln) break;
		p += entry_len(p);
	}

	if (old_pos != NULL) {
		byte *src = old_pos + old_len;
		word move_len = (word)((program + program_len) - src);
		memmove_local(old_pos, src, move_len);
		program_len -= old_len;
		p = old_pos;
	}

	if (text_len == 0) {
		program[program_len] = 0;
		program[program_len + 1] = 0;
		return;
	}

	if (program_len + new_len > PROGRAM_SIZE - 2) {
		printf("OUT OF MEMORY\n");
		return;
	}

	{
		byte *src = p;
		byte *dst = p + new_len;
		word move_len = (word)((program + program_len) - src);
		memmove_local(dst, src, move_len);
	}
	p[0] = ln & 0xff;
	p[1] = (ln >> 8) & 0xff;
	{
		word i;
		for (i = 0; i < text_len; i++) p[2 + i] = text[i];
		p[2 + text_len] = 0;
	}
	program_len += new_len;
	program[program_len] = 0;
	program[program_len + 1] = 0;
}

void do_new() {
	program_len = 0;
	program[0] = 0;
	program[1] = 0;
	for_sp = 0;
	gosub_sp = 0;
	running = 0;
}

void do_list() {
	byte *q = program;
	word ln;
	byte page_lines = 0;
	byte key;
	while (q < program + program_len) {
		ln = q[0] | (q[1] << 8);
		print_int_signed((int)ln);
		putchar(' ');
		printf((char *)(q + 2));
		putchar('\n');
		q += entry_len(q);
		page_lines++;
		if (page_lines >= 15 && q < program + program_len) {
			printf("-- MORE --\n");
			for (;;) {
				key = basic_keyboard_inkey();
				if (key != KEY_CHARCODE_NONE) break;
			}
			if (key == KEY_ESCAPE) break;
			page_lines = 0;
		}
	}
}

void do_save() {
	word i;
	for (i = 0; i < program_len + 2; i++) saved_program[i] = program[i];
	saved_program_len = program_len;
	has_saved = 1;
	printf("SAVED\n");
}

void do_load() {
	word i;
	if (!has_saved) { printf("NO SAVED PROGRAM\n"); return; }
	for (i = 0; i < saved_program_len + 2; i++) program[i] = saved_program[i];
	program_len = saved_program_len;
	for_sp = 0;
	gosub_sp = 0;
	printf("LOADED\n");
}

#define SERIAL_EOF 0x1a	// classic text-file EOF marker (CP/M etc.), used to end a CSAVE/CLOAD stream

// Stream the whole program out as plain text lines over the parallel port
// (softuart). Optional slot number (0-9, default 0) is sent as a 2-byte
// prefix "S<digit>" first, so a listening device (see esp_basic_store/)
// can automatically file it away without needing a web UI click at the
// exact right moment - it just needs to log bytes after the prefix until
// the 0x1A (EOF) byte.
void do_csave(char **pp) {
	char *p = *pp;
	byte *q = program;
	word ln, n;
	char numbuf[7];
	byte ni, slot;
	char *s;

	slot = 0;
	skip_spaces(&p);
	if (*p >= '0' && *p <= '9') slot = (byte)parse_expr(&p);
	if (slot > 9) slot = 9;

	printf("CSAVE ");
	print_int_signed((int)slot);
	putchar('\n');

	softuart_sendByte('S');
	softuart_sendByte('0' + slot);
	while (q < program + program_len) {
		ln = q[0] | (q[1] << 8);
		ni = 0;
		n = ln;
		if (n == 0) numbuf[ni++] = '0';
		while (n > 0) { numbuf[ni++] = '0' + (n % 10); n /= 10; }
		while (ni > 0) softuart_sendByte(numbuf[--ni]);
		softuart_sendByte(' ');
		s = (char *)(q + 2);
		while (*s) softuart_sendByte(*s++);
		softuart_sendByte('\n');
		q += entry_len(q);
	}
	softuart_sendByte(SERIAL_EOF);
	printf("DONE\n");
	*pp = p;
}

// Reads text lines from the parallel port (softuart) and feeds each one
// through handle_input_line(), exactly as if it had been typed. Sends an
// "L<digit>" prefix first (see do_csave()) so a listening device knows
// which of its slots to send back, then waits for it to stream the text,
// ending with a 0x1A (EOF) byte. Press any key to abort while waiting.
void do_cload(char **pp) {
	char *p = *pp;
	char linebuf[80];
	byte li, slot;
	int c;

	slot = 0;
	skip_spaces(&p);
	if (*p >= '0' && *p <= '9') slot = (byte)parse_expr(&p);
	if (slot > 9) slot = 9;

	printf("CLOAD ");
	print_int_signed((int)slot);
	printf(" - waiting (any key=abort)\n");

	softuart_sendByte('L');
	softuart_sendByte('0' + slot);

	do_new();
	li = 0;
	for (;;) {
		c = softuart_receiveByte();
		if (c < 0) {
			if (basic_keyboard_inkey() != KEY_CHARCODE_NONE) { printf("CANCELLED\n"); *pp = p; return; }
			continue;
		}
		if (c == SERIAL_EOF) break;
		if (c == '\r') continue;
		if (c == '\n') {
			linebuf[li] = 0;
			handle_input_line(linebuf);
			li = 0;
			continue;
		}
		if (li < sizeof(linebuf) - 1) linebuf[li++] = (byte)c;
	}
	printf("LOADED\n");
	*pp = p;
}


// ---- Statement implementations -----------------------------------------

void do_goto_line(word ln) {
	byte *t = find_line_exact(ln);
	if (t == NULL) {
		error("UNDEFINED LINE");
		running = 0;
		return;
	}
	jump_flag = 1;
	jump_target = t;
}

void do_let(char **pp) {
	char *p = *pp;
	byte idx;
	int v;
	skip_spaces(&p);
	if (*p < 'A' || *p > 'Z') { error("SYNTAX"); while (*p) p++; *pp = p; return; }
	idx = *p - 'A';
	p++;
	skip_spaces(&p);
	if (*p != '=') { error("SYNTAX"); while (*p) p++; *pp = p; return; }
	p++;
	v = parse_expr(&p);
	vars[idx] = v;
	*pp = p;
}

void do_print(char **pp) {
	char *p = *pp;
	byte nl = 1;
	int v;
	for (;;) {
		skip_spaces(&p);
		if (*p == 0 || *p == ':') break;
		if (*p == '"') {
			p++;
			while (*p && *p != '"') { putchar(*p); p++; }
			if (*p == '"') p++;
		} else {
			v = parse_expr(&p);
			print_int_signed(v);
		}
		skip_spaces(&p);
		if (*p == ',') { putchar(' '); p++; nl = 1; continue; }
		if (*p == ';') {
			p++;
			nl = 0;
			skip_spaces(&p);
			if (*p == 0 || *p == ':') break;
			nl = 1;
			continue;
		}
		break;
	}
	if (nl) putchar('\n');
	*pp = p;
}

// IF <expr> THEN <line> [ELSE <line>]   (numeric branch targets, with ELSE)
// IF <expr> THEN <stmt>                 (inline statement, no ELSE support -
//                                         skipping an un-executed statement
//                                         without running it isn't worth the
//                                         extra parsing complexity here)
void do_if(char **pp) {
	char *p = *pp;
	int cond;
	word then_ln, else_ln;
	byte have_else = 0;
	else_ln = 0;
	cond = parse_expr(&p);
	skip_spaces(&p);
	if (!match_keyword(&p, "THEN")) {
		error("IF WITHOUT THEN");
		while (*p) p++;
		*pp = p;
		return;
	}
	skip_spaces(&p);
	if (*p >= '0' && *p <= '9') {
		then_ln = 0;
		while (*p >= '0' && *p <= '9') { then_ln = then_ln * 10 + (*p - '0'); p++; }
		skip_spaces(&p);
		if (match_keyword(&p, "ELSE")) {
			skip_spaces(&p);
			else_ln = 0;
			while (*p >= '0' && *p <= '9') { else_ln = else_ln * 10 + (*p - '0'); p++; }
			have_else = 1;
		}
		if (cond) {
			do_goto_line(then_ln);
		} else if (have_else) {
			do_goto_line(else_ln);
		}
		*pp = p;
		return;
	}
	if (cond) {
		exec_statement(&p);
	} else {
		while (*p) p++;
	}
	*pp = p;
}

void do_goto(char **pp) {
	char *p = *pp;
	int v = parse_expr(&p);
	do_goto_line((word)v);
	*pp = p;
}

void do_gosub(char **pp) {
	char *p = *pp;
	int v = parse_expr(&p);
	if (gosub_sp >= GOSUB_STACK_MAX) {
		error("GOSUB TOO DEEP");
		while (*p) p++;
		*pp = p;
		return;
	}
	gosub_stack[gosub_sp++] = next_line_ptr();
	do_goto_line((word)v);
	*pp = p;
}

void do_return(char **pp) {
	char *p = *pp;
	if (gosub_sp == 0) {
		error("RETURN WITHOUT GOSUB");
		*pp = p;
		return;
	}
	gosub_sp--;
	jump_flag = 1;
	jump_target = gosub_stack[gosub_sp];
	*pp = p;
}

void do_for(char **pp) {
	char *p = *pp;
	byte var;
	int start, limit, step;
	t_for_frame *f;
	skip_spaces(&p);
	if (*p < 'A' || *p > 'Z') { error("FOR SYNTAX"); while (*p) p++; *pp = p; return; }
	var = *p - 'A';
	p++;
	skip_spaces(&p);
	if (*p != '=') { error("FOR SYNTAX"); while (*p) p++; *pp = p; return; }
	p++;
	start = parse_expr(&p);
	skip_spaces(&p);
	if (!match_keyword(&p, "TO")) { error("FOR WITHOUT TO"); while (*p) p++; *pp = p; return; }
	limit = parse_expr(&p);
	skip_spaces(&p);
	step = 1;
	if (match_keyword(&p, "STEP")) step = parse_expr(&p);
	if (for_sp >= FOR_STACK_MAX) { error("FOR TOO DEEP"); while (*p) p++; *pp = p; return; }
	f = &for_stack[for_sp++];
	f->var = var;
	f->cur = start;
	f->limit = limit;
	f->step = step;
	f->body_start = next_line_ptr();
	vars[var] = start;
	*pp = p;
}

void do_next(char **pp) {
	char *p = *pp;
	t_for_frame *f;
	skip_spaces(&p);
	if (*p >= 'A' && *p <= 'Z') p++;	// optional loop variable name, ignored
	if (for_sp == 0) {
		error("NEXT WITHOUT FOR");
		*pp = p;
		return;
	}
	f = &for_stack[for_sp - 1];
	f->cur += f->step;
	vars[f->var] = f->cur;
	if ((f->step > 0 && f->cur <= f->limit) || (f->step < 0 && f->cur >= f->limit)) {
		jump_flag = 1;
		jump_target = f->body_start;
	} else {
		for_sp--;
	}
	*pp = p;
}

void do_input(char **pp) {
	char *p = *pp;
	byte var;
	char buf[16];
	int v;
	byte neg, i;
	for (;;) {
		skip_spaces(&p);
		if (*p < 'A' || *p > 'Z') { error("INPUT SYNTAX"); break; }
		var = *p - 'A';
		p++;
		printf("? ");
		gets(buf);
		i = 0;
		neg = 0;
		v = 0;
		if (buf[i] == '-') { neg = 1; i++; }
		while (buf[i] >= '0' && buf[i] <= '9') { v = v * 10 + (buf[i] - '0'); i++; }
		if (neg) v = -v;
		vars[var] = v;
		skip_spaces(&p);
		if (*p == ',') { p++; continue; }
		break;
	}
	*pp = p;
}

void do_poke(char **pp) {
	char *p = *pp;
	int addr, val;
	addr = parse_expr(&p);
	skip_spaces(&p);
	if (*p == ',') p++;
	val = parse_expr(&p);
	*(byte *)(word)addr = (byte)val;
	*pp = p;
}

void do_plot(char **pp) {
	char *p = *pp;
	int x, y, c;
	x = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	y = parse_expr(&p);
	c = 1;
	skip_spaces(&p);
	if (*p == ',') { p++; c = parse_expr(&p); }
	plot_pixel(x, y, (byte)c);
	*pp = p;
}

void do_lineto(char **pp) {
	char *p = *pp;
	int x0, y0, x1, y1;
	x0 = parse_expr(&p); skip_spaces(&p); if (*p == ',') p++;
	y0 = parse_expr(&p); skip_spaces(&p); if (*p == ',') p++;
	x1 = parse_expr(&p); skip_spaces(&p); if (*p == ',') p++;
	y1 = parse_expr(&p);
	draw_line(x0, y0, x1, y1, 1);
	*pp = p;
}

void do_sound(char **pp) {
	char *p = *pp;
	int f, l;
	f = parse_expr(&p);
	skip_spaces(&p); if (*p == ',') p++;
	l = parse_expr(&p);
	sound_tone((word)f, (word)l);
	*pp = p;
}

// Busy-wait delay for game timing (no hardware timer on this platform).
// n is roughly milliseconds, very approximate - tune per real hardware/MAME.
void do_pause(char **pp) {
	char *p = *pp;
	int n;
	n = parse_expr(&p);
	while (n > 0 && running) {
		if (check_break()) { running = 0; break; }
		delay_nop(200);
		n--;
	}
	*pp = p;
}


// ---- Statement dispatch ------------------------------------------------

void exec_statement(char **pp) {
	char *p = *pp;
	skip_spaces(&p);
	if (*p == 0 || *p == ':') { *pp = p; return; }

	if (match_keyword(&p, "REM")) { while (*p) p++; *pp = p; return; }
	if (match_keyword(&p, "LET")) { do_let(&p); *pp = p; return; }
	if (match_keyword(&p, "PRINT")) { do_print(&p); *pp = p; return; }
	if (match_keyword(&p, "IF")) { do_if(&p); *pp = p; return; }
	if (match_keyword(&p, "GOSUB")) { do_gosub(&p); *pp = p; return; }
	if (match_keyword(&p, "GOTO")) { do_goto(&p); *pp = p; return; }
	if (match_keyword(&p, "RETURN")) { do_return(&p); *pp = p; return; }
	if (match_keyword(&p, "FOR")) { do_for(&p); *pp = p; return; }
	if (match_keyword(&p, "NEXT")) { do_next(&p); *pp = p; return; }
	if (match_keyword(&p, "INPUT")) { do_input(&p); *pp = p; return; }
	if (match_keyword(&p, "POKE")) { do_poke(&p); *pp = p; return; }
	if (match_keyword(&p, "PLOT")) { do_plot(&p); *pp = p; return; }
	if (match_keyword(&p, "LINE")) { do_lineto(&p); *pp = p; return; }
	if (match_keyword(&p, "DEFSP")) { do_defsprite(&p); *pp = p; return; }
	if (match_keyword(&p, "DRASP")) { do_drawsprite(&p); *pp = p; return; }
	if (match_keyword(&p, "MOVSP")) { do_movesprite(&p); *pp = p; return; }
	if (match_keyword(&p, "SOUND")) { do_sound(&p); *pp = p; return; }
	if (match_keyword(&p, "BEEP")) { beep(); *pp = p; return; }
	if (match_keyword(&p, "CLS")) { lcd_clear(); *pp = p; return; }
	if (match_keyword(&p, "PAUSE")) { do_pause(&p); *pp = p; return; }
	if (match_keyword(&p, "END")) { running = 0; *pp = p; return; }
	if (match_keyword(&p, "STOP")) { running = 0; *pp = p; return; }
	if (match_keyword(&p, "LIST")) { do_list(); *pp = p; return; }
	if (match_keyword(&p, "NEW")) { do_new(); *pp = p; return; }
	if (match_keyword(&p, "SAVE")) { do_save(); *pp = p; return; }
	if (match_keyword(&p, "LOAD")) { do_load(); *pp = p; return; }
	if (match_keyword(&p, "CSAVE")) { do_csave(&p); *pp = p; return; }
	if (match_keyword(&p, "CLOAD")) { do_cload(&p); *pp = p; return; }

	if (*p >= 'A' && *p <= 'Z') { do_let(&p); *pp = p; return; }

	error("SYNTAX ERROR");
	while (*p) p++;
	*pp = p;
}


// ---- RUN loop -----------------------------------------------------------

void do_run() {
	byte *p = program;
	char *stp;

	running = 1;
	for_sp = 0;
	gosub_sp = 0;

	while (running && p < program + program_len) {
		if (check_break()) {
			running = 0;
			break;
		}
		line_start = p;
		cur_line = p[0] | (p[1] << 8);
		stp = (char *)(p + 2);
		jump_flag = 0;

		while (*stp && running) {
			if (check_break()) {
				running = 0;
				break;
			}
			exec_statement(&stp);
			if (jump_flag) break;
			skip_spaces(&stp);
			if (*stp == ':') { stp++; continue; }
			break;
		}

		if (jump_flag) {
			p = jump_target;
			jump_flag = 0;
		} else {
			p = p + entry_len(p);
		}
	}
	printf("READY\n");
}


// ---- Line editor: arrow-key cursor movement + blinking cursor block -----
//
// There is no hardware timer/interrupt on this platform (see the pacman/
// roguelike examples), so the cursor blink is just a busy-wait poll loop:
// keyboard_inkey() is non-blocking (returns KEY_CHARCODE_NONE if nothing is
// pressed), so we toggle the cursor a few thousand idle iterations while
// waiting for the next key - the same trick as pacman.c's delay(). Tune
// BLINK_PERIOD if the blink looks too fast/slow on real hardware.
#define BLINK_PERIOD 1500

void delay_nop(word n) {
	word i;
	for (i = 0; i < n; i++) {
		__asm
			nop
		__endasm;
	}
}

// XOR the pixels of one text cell (used to draw/undraw the cursor block).
// Works for any font width, incl. ones that aren't a multiple of 8 bits.
void invert_cell(byte col, byte row) {
	byte x = col * font_char_width;
	byte y = row * font_char_height;
	byte screen_bx = x & 7;
	byte w = font_char_width;
	byte mask, mask2;
	int over;
	byte iy;
	byte *p;

	if (y + font_char_height > LCD_H) return;

	mask = (byte)(0xff << (8 - w));	// top w bits set
	mask = mask >> screen_bx;
	over = (int)screen_bx + (int)w - 8;
	mask2 = (over > 0) ? (byte)(0xff << (8 - over)) : 0;

	p = (byte *)lcd_addr + (word)y * LCD_SCANLINE_SIZE + (x >> 3);
	for (iy = 0; iy < font_char_height; iy++) {
		*p ^= mask;
		if (over > 0) *(p + 1) ^= mask2;
		p += LCD_SCANLINE_SIZE;
	}
}

// Reads one line of input at the current cursor position, in place in buf
// (buf may already contain a pre-filled string, e.g. from EDIT - editing
// starts with the cursor at its end). Supports LEFT/RIGHT to move within
// the line, BACKSPACE/DELETE, and shows a blinking inverted cursor block.
// Limitation: assumes the line fits in one screen row (no wrap-aware
// cursor math) - fine for default 60-col font and typical BASIC lines.
void input_line(char *buf, byte maxlen) {
	byte len, cur, i;
	byte start_col, start_row;
	byte blink_on;
	word blink_timer;
	byte c;

	len = (byte)strlen_local(buf);
	cur = len;
	start_col = lcd_text_col;
	start_row = lcd_text_row;
	blink_on = 0;
	blink_timer = 0;

	for (;;) {
		lcd_text_col = start_col;
		lcd_text_row = start_row;
		for (i = 0; i < len; i++) lcd_putchar_at(start_col + i, start_row, buf[i]);
		lcd_putchar_at(start_col + len, start_row, ' ');	// clear leftover char if line shrank

		if (blink_on) invert_cell(start_col + cur, start_row);

		c = basic_keyboard_inkey();

		if (blink_on) invert_cell(start_col + cur, start_row);	// undraw before redrawing text next loop

		if (c == KEY_CHARCODE_NONE) {
			delay_nop(200);
			blink_timer++;
			if (blink_timer > BLINK_PERIOD) {
				blink_timer = 0;
				blink_on = !blink_on;
			}
			continue;
		}

		blink_on = 0;
		blink_timer = 0;

		if (c == KEY_ENTER || c == '\n' || c == '\r') {
			buf[len] = 0;
			lcd_text_col = start_col;
			lcd_text_row = start_row;
			for (i = 0; i < len; i++) lcd_putchar_at(start_col + i, start_row, buf[i]);
			lcd_text_col = len;
			putchar('\n');
			return;
		}
		if (c == KEY_LEFT) { if (cur > 0) cur--; continue; }
		if (c == KEY_RIGHT) { if (cur < len) cur++; continue; }
		if (c == KEY_BACKSPACE) {
			if (cur > 0) {
				for (i = cur - 1; i < len - 1; i++) buf[i] = buf[i+1];
				len--;
				cur--;
			}
			continue;
		}
		if (c == KEY_DELETE) {
			if (cur < len) {
				for (i = cur; i < len - 1; i++) buf[i] = buf[i+1];
				len--;
			}
			continue;
		}
		if (c >= 32 && c < 127 && len < maxlen - 1) {
			for (i = len; i > cur; i--) buf[i] = buf[i-1];
			buf[cur] = c;
			len++;
			cur++;
		}
	}
}


// ---- REPL / immediate mode ------------------------------------------------

void do_edit(word ln) {
	byte *t = find_line_exact(ln);
	char editbuf[80];
	char *d, *s;
	char numbuf[7];
	byte ni = 0;
	word n = ln;

	if (t == NULL) { printf("UNDEFINED LINE\n"); return; }

	d = editbuf;
	if (n == 0) numbuf[ni++] = '0';
	while (n > 0) { numbuf[ni++] = '0' + (n % 10); n /= 10; }
	while (ni > 0) *d++ = numbuf[--ni];
	*d++ = ' ';
	s = (char *)(t + 2);
	while (*s) *d++ = *s++;
	*d = 0;

	printf("> ");
	input_line(editbuf, sizeof(editbuf));
	handle_input_line(editbuf);
}

void handle_input_line(char *line) {
	char *p = line;
	word ln;
	skip_spaces(&p);
	if (*p == 0) return;

	if (*p >= '0' && *p <= '9') {
		ln = 0;
		while (*p >= '0' && *p <= '9') { ln = ln * 10 + (*p - '0'); p++; }
		skip_spaces(&p);
		normalize_defsprite(p);
		store_line(ln, p);
		return;
	}

	if (match_keyword(&p, "RUN")) { do_run(); return; }
	if (match_keyword(&p, "LIST")) { do_list(); return; }
	if (match_keyword(&p, "NEW")) { do_new(); return; }
	if (match_keyword(&p, "EDIT")) {
		skip_spaces(&p);
		ln = 0;
		while (*p >= '0' && *p <= '9') { ln = ln * 10 + (*p - '0'); p++; }
		do_edit(ln);
		return;
	}

	{
		char *pp = p;
		running = 0;
		while (*pp) {
			exec_statement(&pp);
			if (jump_flag) { jump_flag = 0; break; }	// no GOTO/loop support outside RUN
			skip_spaces(&pp);
			if (*pp == ':') { pp++; continue; }
			break;
		}
	}
}

void main() {
	char line[80];
	byte i;

	program_len = 0;
	program[0] = 0;
	program[1] = 0;
	saved_program_len = 0;
	has_saved = 0;
	for (i = 0; i < 26; i++) vars[i] = 0;
	for_sp = 0;
	gosub_sp = 0;
	running = 0;

	lcd_clear();
	printf("VGL BASIC\n");
	printf("READY\n");

	for (;;) {
		printf("> ");
		line[0] = 0;
		input_line(line, sizeof(line));
		handle_input_line(line);
	}
}

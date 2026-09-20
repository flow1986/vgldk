/*
	Dungeon Bash for the VGLDK (GL6000SL)

	A tile/sprite port of the ideas behind "Dungeon Bash" (Martin Read,
	2005, dungeonbash-1.7.tar, GPLv3 - see /LICENSE and the original
	sources at the repo root: dunbash.h, permons.c, permobj.c, map.c,
	combat.c, ...).

	The original is a curses/POSIX terminal roguelike: dynamic memory,
	a 42x42 dungeon level kept as several parallel `int[42][42]` arrays,
	a 21x21 character viewport scrolled to follow the player, and a
	turn-based command loop reading from a termios raw terminal.

	None of the curses/POSIX plumbing survives a port to a Z80 cart with
	~qKB of banked RAM, so this example is a from-scratch reimplementation
	of the *architecture* (scrolling camera over a bigger byte-sized map,
	turn-based bump combat, permanent monster/object tables as static
	const data) using real 8x8 bitmap sprites instead of ASCII glyphs.
	permons.c/permobj.c-style stat data for a handful of monsters, plus a
	first slice of the inventory (weapon/armour auto-equip) and magic
	system (healing potions, teleport scrolls) was carried over by hand
	(see monster_defs[]/item_defs[] below) as a starting point; the rest
	of the original game logic (spellcasting, curses, rings, full 19-slot
	inventory UI, multiple dungeon levels, ...) is intentionally NOT
	ported yet.

	Viewport/camera note (see info/ session notes for the discussion):
	the original's "windowed copy of a back-buffer, centered on the
	player, redrawn only where changed" technique maps 1:1 onto sprite
	tiles (draw_tile() below plays the role of newsym()+draw_world()).
	What does NOT carry over unchanged is the *size* of that window:
	21x21 characters would be 168x168 pixels, but the GL6000SL LCD is
	only 240x100 1bpp pixels. So the camera logic is identical, but
	VIEW_W/VIEW_H had to shrink to 30x10 tiles (240x80px) with a 16px
	HUD strip on top, instead of 21x21.

	Multiplayer note: player state is a `players[]` array with an
	`active_player` index from the very start (instead of a single
	global `u` like the original), so a later multiplayer mode is just
	"more entries", not a rewrite. Planned (NOT implemented yet) design:
	an ESP8266 attached via the parallel/printer port acts as the network
	bridge to a dedicated external multiplayer server - neither split-
	screen (screen is far too small for two viewports) nor hot-seat
	(shared screen, alternating local turns). For now this file only
	runs a single local player; the 2-player hot-seat code path further
	down is a leftover local test of the players[] array, not the
	intended multiplayer model.

	2026-09-20 (VGLDK port scaffold)
*/

#include <vgldk.h>
#include <stdiomin.h>


// ---- Grid / camera -------------------------------------------------------

#define TILE 8	// sprite size in pixels. Must stay 8: draw_tile() relies on
				// tiles being exactly one byte wide in the framebuffer.

#define GRID_X 0	// pixel origin of the viewport (must be a multiple of 8!)
#define GRID_Y 16	// leaves 2 text rows on top for HUD + message line

#define VIEW_W 30	// 30*8 = 240 = full LCD width
#define VIEW_H 10	// 10*8 = 80, fits under the 16px HUD strip (100-16=84)

// The dungeon is bigger than the viewport, so the camera scrolls
// (same idea as the original's DUN_WIDTH/DUN_HEIGHT=42 vs. DISP=21).
#define DUN_W 48
#define DUN_H 24


// ---- Terrain --------------------------------------------------------------

#define T_WALL 0
#define T_FLOOR 1
#define T_DOOR 2
#define T_STAIRS 3

byte terrain[DUN_H][DUN_W];
byte mapmonster[DUN_H][DUN_W];	// index into monsters[], or NO_MONSTER


// ---- 8x8 sprites (1 bit per pixel, MSB = leftmost pixel) -----------------
// For a first pass, every ASCII glyph the original used (via permons[].sym /
// permobjs[].sym / terrain_char()) gets a hand-drawn 8x8 bitmap equivalent.

const byte spr_wall[TILE] = {
	0xff, 0x81, 0xbd, 0xbd, 0x81, 0xbd, 0xbd, 0xff,
};
const byte spr_floor[TILE] = {
	0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x80, 0x00,
};
const byte spr_door[TILE] = {
	0x00, 0x7e, 0x42, 0x5a, 0x42, 0x42, 0x7e, 0x00,
};
const byte spr_stairs[TILE] = {
	0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x80,
};
const byte spr_gold[TILE] = {
	0x00, 0x3c, 0x66, 0x5a, 0x5a, 0x66, 0x3c, 0x00,
};

const byte spr_player1[TILE] = {
	0x3c, 0x7e, 0x5a, 0x7e, 0x3c, 0x18, 0x3c, 0x66,
};
const byte spr_player2[TILE] = {
	0x3c, 0x42, 0x99, 0x81, 0x81, 0x99, 0x42, 0x3c,
};

// One sprite per permon-style monster type (see monster_defs[] below)
const byte spr_newt[TILE] = {
	0x00, 0x22, 0x77, 0x3e, 0x3e, 0x77, 0x22, 0x00,
};
const byte spr_rat[TILE] = {
	0x00, 0x60, 0xf8, 0xfe, 0xff, 0x7d, 0x39, 0x00,
};
const byte spr_snake[TILE] = {
	0x38, 0x44, 0x38, 0x10, 0x10, 0x08, 0x04, 0x00,
};
const byte spr_wolf[TILE] = {
	0x42, 0xe7, 0x7e, 0xff, 0xdb, 0xff, 0x81, 0x00,
};
const byte spr_thug[TILE] = {
	0x18, 0x18, 0x7e, 0xff, 0xdb, 0xff, 0x66, 0x66,
};
const byte spr_goon[TILE] = {
	0x3c, 0x3c, 0xff, 0xff, 0xff, 0xdb, 0x66, 0x66,
};
const byte spr_hunter[TILE] = {
	0x08, 0x1c, 0x3e, 0x2a, 0x3e, 0x1c, 0x08, 0x14,
};
const byte spr_goblin[TILE] = {
	0x00, 0x24, 0x3c, 0x7e, 0x5a, 0x3c, 0x24, 0x00,
};
const byte spr_troll[TILE] = {
	0x66, 0xff, 0xff, 0xff, 0xff, 0xdb, 0xc3, 0x66,
};
const byte spr_zombie[TILE] = {
	0x3c, 0x7e, 0x5b, 0x7e, 0x3c, 0x18, 0x24, 0x42,
};

// Inventory/magic item sprites
const byte spr_weapon[TILE] = {
	0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00,
};
const byte spr_armour[TILE] = {
	0x18, 0x24, 0x42, 0x42, 0xff, 0xff, 0x66, 0x66,
};
const byte spr_potion[TILE] = {
	0x00, 0x18, 0x18, 0x3c, 0x66, 0x66, 0x66, 0x3c,
};
const byte spr_scroll[TILE] = {
	0x00, 0x7e, 0xff, 0xa5, 0xa5, 0xff, 0x7e, 0x00,
};


// ---- Monster types (stat subset of permons.c, hand-carried) --------------

#define MT_NEWT 0
#define MT_RAT 1
#define MT_SNAKE 2
#define MT_WOLF 3
#define MT_THUG 4
#define MT_GOON 5
#define MT_HUNTER 6
#define MT_GOBLIN 7
#define MT_TROLL 8
#define MT_ZOMBIE 9
#define NUM_MONSTER_TYPES 10

typedef struct {
	const byte *sprite;
	byte hpmax;
	byte atk;
	byte def;
} t_monster_def;

// Rough stat subset lifted from permons.c: name/hp/atk/def, roughly in
// increasing order of danger (same grouping the original uses).
const t_monster_def monster_defs[NUM_MONSTER_TYPES] = {
	{ spr_newt,    3,  1, 0 },	// PM_NEWT
	{ spr_rat,     4,  2, 0 },	// PM_RAT
	{ spr_snake,   6,  3, 1 },	// PM_SNAKE
	{ spr_wolf,   10,  5, 2 },	// PM_WOLF
	{ spr_thug,    8,  4, 1 },	// PM_THUG
	{ spr_goblin, 12,  5, 1 },	// PM_GOBLIN
	{ spr_goon,   14,  6, 2 },	// PM_GOON
	{ spr_hunter, 16,  7, 2 },	// PM_HUNTER
	{ spr_zombie, 20,  6, 3 },	// PM_ZOMBIE
	{ spr_troll,  30,  9, 4 },	// PM_TROLL
};

// Short display names, same order as monster_defs[] (for HUD/messages)
const char *monster_names[NUM_MONSTER_TYPES] = {
	"Molch", "Ratte", "Schlange", "Wolf", "Schlaeger",
	"Goblin", "Rabauke", "Jaeger", "Zombie", "Troll",
};

#define NO_MONSTER 0xff
#define MAX_MONSTERS 12

typedef struct {
	byte x, y;
	byte type;
	byte hp;
	byte alive;
} t_monster;

t_monster monsters[MAX_MONSTERS];
byte num_monsters;


// ---- Gold piles (kept as a small list instead of a full object grid) -----

#define MAX_GOLD 8
byte gold_x[MAX_GOLD];
byte gold_y[MAX_GOLD];
byte gold_alive[MAX_GOLD];
byte num_gold;


// ---- Items (subset of permobj.c: weapons, armour, potions, scrolls) ------
// Weapons/armour auto-equip on pickup (better bonus wins); potions/scrolls
// go into a small per-player consumable count instead of a full 19-slot
// inventory - a first slice of the original's object/magic system, not
// the whole of objects.c/bmagic.c.

#define IK_WEAPON 0
#define IK_ARMOUR 1
#define IK_POTION 2
#define IK_SCROLL 3

#define IT_DAGGER 0
#define IT_SWORD 1
#define IT_LEATHER 2
#define IT_CHAIN 3
#define IT_POT_HEAL 4
#define IT_SCR_TELEPORT 5
#define NUM_ITEM_TYPES 6

typedef struct {
	const byte *sprite;
	byte kind;
	byte bonus;	// weapon: atk bonus, armour: def bonus, potion: heal amount
} t_item_def;

const t_item_def item_defs[NUM_ITEM_TYPES] = {
	{ spr_weapon, IK_WEAPON,  2 },	// PO_DAGGER
	{ spr_weapon, IK_WEAPON,  6 },	// PO_LONG_SWORD
	{ spr_armour, IK_ARMOUR,  2 },	// PO_LEATHER_ARMOUR
	{ spr_armour, IK_ARMOUR,  4 },	// PO_CHAINMAIL
	{ spr_potion, IK_POTION, 10 },	// PO_POT_HEAL
	{ spr_scroll, IK_SCROLL,  0 },	// PO_SCR_TELEPORT
};

#define MAX_ITEMS 10
byte item_x[MAX_ITEMS];
byte item_y[MAX_ITEMS];
byte item_type[MAX_ITEMS];
byte item_alive[MAX_ITEMS];
byte num_items;


// ---- Players --------------------------------------------------------------
// Kept as an array + "active_player" index from the start so a later
// networked multiplayer mode (ESP8266 on the parallel port <-> a
// dedicated multiplayer server - see file header) is just "more entries",
// not a rewrite. Not implemented yet: single local player for now.

#define MAX_PLAYERS 2
#define NUM_PLAYERS 1	// local player count; set to 2 only to test the players[] array (hot-seat, not the intended multiplayer design)

typedef struct {
	byte x, y;
	int hp, hpmax;
	byte atk, def;	// base stats
	byte weapon_bonus, armour_bonus;	// from equipped items
	byte potions, scrolls;	// consumable counts (simplified inventory)
	word gold;
	byte alive;
} t_player;

t_player players[MAX_PLAYERS];
byte active_player;

word score;
word high_score;	// kept across restarts (until power-off), no EEPROM save yet

char msg[21];	// bottom message line


// ---- Tiny RNG (SDCC/Z80-friendly, no libc rand()/time()) ------------------

word rng_seed = 0x2A2Au;
word rng16(void) {
	rng_seed = rng_seed * 25173u + 13849u;
	return rng_seed;
}
byte rnd_range(byte lo, byte hi) {
	// hi inclusive
	return lo + (byte)(rng16() % (word)(hi - lo + 1));
}


// ---- Low level drawing -----------------------------------------------------

// Blit an 8x8 sprite at viewport tile coordinates (tx,ty). GRID_X is
// byte-aligned and TILE==8, so every tile is exactly one framebuffer byte
// wide - no bit-shifting needed (unlike lcd_draw_glypth_at() for text).
void draw_tile(byte tx, byte ty, const byte *sprite) {
	byte *p;
	byte iy;
	word offset;

	offset = (word)(GRID_Y + (word)ty * TILE) * LCD_SCANLINE_SIZE + (GRID_X / 8) + tx;
	p = (byte *)lcd_addr + offset;

	for (iy = 0; iy < TILE; iy++) {
		*p = sprite[iy];
		p += LCD_SCANLINE_SIZE;
	}
}

void print_text(byte col, byte row, const char *s) {
	byte x;
	x = col;
	while (*s != 0) {
		lcd_putchar_at(x, row, *s);
		s++;
		x++;
	}
}

void print_num2(byte col, byte row, byte v) {
	lcd_putchar_at(col,   row, '0' + (v / 10) % 10);
	lcd_putchar_at(col+1, row, '0' + v % 10);
}

void print_num4(byte col, byte row, word v) {
	lcd_putchar_at(col,   row, '0' + (byte)((v / 1000) % 10));
	lcd_putchar_at(col+1, row, '0' + (byte)((v / 100) % 10));
	lcd_putchar_at(col+2, row, '0' + (byte)((v / 10) % 10));
	lcd_putchar_at(col+3, row, '0' + (byte)(v % 10));
}

// Like print_text(), but clears the rest of the 21-char message row first -
// needed because monster-name/HP messages vary in length between frames.
void print_line(byte row, const char *s) {
	byte x;
	x = 0;
	while (*s != 0 && x < 21) {
		lcd_putchar_at(x, row, *s);
		s++;
		x++;
	}
	while (x < 21) {
		lcd_putchar_at(x, row, ' ');
		x++;
	}
}

// No <string.h>/strcpy() available with stdiomin.h - copy by hand.
void set_msg(const char *s) {
	byte i;
	i = 0;
	while (*s != 0 && i < sizeof(msg) - 1) {
		msg[i] = *s;
		s++;
		i++;
	}
	msg[i] = 0;
}

// Building blocks for dynamic messages (e.g. "<Monstername> HP:3/6").
byte msg_pos;
void msg_clear(void) {
	msg_pos = 0;
	msg[0] = 0;
}
void msg_append(const char *s) {
	while (*s != 0 && msg_pos < sizeof(msg) - 1) {
		msg[msg_pos++] = *s;
		s++;
	}
	msg[msg_pos] = 0;
}
void msg_append_num(word v) {
	char buf[6];
	byte n;
	n = 0;
	if (v >= 1000) buf[n++] = '0' + (v / 1000) % 10;
	if (v >= 100) buf[n++] = '0' + (v / 100) % 10;
	if (v >= 10) buf[n++] = '0' + (v / 10) % 10;
	buf[n++] = '0' + (byte)(v % 10);
	buf[n] = 0;
	msg_append(buf);
}


// ---- Helpers ----------------------------------------------------------

byte monster_at(byte x, byte y) {
	byte i;
	for (i = 0; i < num_monsters; i++) {
		if (monsters[i].alive && monsters[i].x == x && monsters[i].y == y)
			return i;
	}
	return NO_MONSTER;
}

byte player_at(byte x, byte y, byte exclude) {
	byte i;
	for (i = 0; i < NUM_PLAYERS; i++) {
		if (i != exclude && players[i].alive && players[i].x == x && players[i].y == y)
			return i;
	}
	return 0xff;
}

byte gold_at(byte x, byte y) {
	byte i;
	for (i = 0; i < num_gold; i++) {
		if (gold_alive[i] && gold_x[i] == x && gold_y[i] == y)
			return i;
	}
	return 0xff;
}

byte item_at(byte x, byte y) {
	byte i;
	for (i = 0; i < num_items; i++) {
		if (item_alive[i] && item_x[i] == x && item_y[i] == y)
			return i;
	}
	return 0xff;
}

byte player_atk(byte pi) {
	return players[pi].atk + players[pi].weapon_bonus;
}

byte player_def(byte pi) {
	return players[pi].def + players[pi].armour_bonus;
}


// ---- Dungeon generation (simplified stand-in for the original map.c) ------

#define MAX_ROOMS 8
byte room_cx[MAX_ROOMS], room_cy[MAX_ROOMS];
byte num_rooms;

void carve_rect(byte x0, byte y0, byte w, byte h) {
	byte x, y;
	for (y = y0; y < y0 + h; y++) {
		for (x = x0; x < x0 + w; x++) {
			terrain[y][x] = T_FLOOR;
		}
	}
}

void carve_corridor(byte x0, byte y0, byte x1, byte y1) {
	byte x, y;
	x = x0;
	y = y0;
	while (x != x1) {
		terrain[y][x] = T_FLOOR;
		if (x < x1) x++; else x--;
	}
	while (y != y1) {
		terrain[y][x] = T_FLOOR;
		if (y < y1) y++; else y--;
	}
	terrain[y][x] = T_FLOOR;
}

void generate_dungeon(void) {
	byte i, x, y;
	byte rw, rh, rx, ry;

	for (y = 0; y < DUN_H; y++) {
		for (x = 0; x < DUN_W; x++) {
			terrain[y][x] = T_WALL;
			mapmonster[y][x] = NO_MONSTER;
		}
	}

	num_rooms = 0;
	for (i = 0; i < MAX_ROOMS; i++) {
		rw = rnd_range(4, 7);
		rh = rnd_range(3, 5);
		rx = rnd_range(1, DUN_W - rw - 2);
		ry = rnd_range(1, DUN_H - rh - 2);
		carve_rect(rx, ry, rw, rh);
		room_cx[num_rooms] = rx + rw / 2;
		room_cy[num_rooms] = ry + rh / 2;
		if (num_rooms > 0) {
			carve_corridor(room_cx[num_rooms - 1], room_cy[num_rooms - 1],
				room_cx[num_rooms], room_cy[num_rooms]);
		}
		num_rooms++;
	}

	terrain[room_cy[num_rooms - 1]][room_cx[num_rooms - 1]] = T_STAIRS;
}

const byte *terrain_sprite(byte t) {
	switch (t) {
		case T_WALL: return spr_wall;
		case T_DOOR: return spr_door;
		case T_STAIRS: return spr_stairs;
		default: return spr_floor;
	}
}


// ---- Populate monsters & gold ---------------------------------------------

void populate(void) {
	byte i, r, x, y;

	num_monsters = 0;
	for (i = 0; i < MAX_MONSTERS && i < (byte)(num_rooms - 1); i++) {
		r = rnd_range(1, num_rooms - 1);	// avoid room 0 (player start)
		x = room_cx[r];
		y = room_cy[r];
		if (monster_at(x, y) != NO_MONSTER) continue;
		monsters[num_monsters].x = x;
		monsters[num_monsters].y = y;
		monsters[num_monsters].type = rnd_range(0, NUM_MONSTER_TYPES - 1);
		monsters[num_monsters].hp = monster_defs[monsters[num_monsters].type].hpmax;
		monsters[num_monsters].alive = 1;
		mapmonster[y][x] = num_monsters;
		num_monsters++;
	}

	num_gold = 0;
	for (i = 0; i < MAX_GOLD; i++) {
		r = rnd_range(1, num_rooms - 1);
		x = room_cx[r] + rnd_range(0, 1);
		y = room_cy[r];
		if (terrain[y][x] != T_FLOOR) continue;
		if (monster_at(x, y) != NO_MONSTER) continue;
		gold_x[num_gold] = x;
		gold_y[num_gold] = y;
		gold_alive[num_gold] = 1;
		num_gold++;
	}

	num_items = 0;
	for (i = 0; i < MAX_ITEMS; i++) {
		r = rnd_range(1, num_rooms - 1);
		x = room_cx[r] + rnd_range(0, 1);
		y = room_cy[r] + rnd_range(0, 1);
		if (terrain[y][x] != T_FLOOR) continue;
		if (monster_at(x, y) != NO_MONSTER) continue;
		if (gold_at(x, y) != 0xff) continue;
		item_x[num_items] = x;
		item_y[num_items] = y;
		item_type[num_items] = rnd_range(0, NUM_ITEM_TYPES - 1);
		item_alive[num_items] = 1;
		num_items++;
	}
}

void init_players(void) {
	byte i;
	for (i = 0; i < NUM_PLAYERS; i++) {
		players[i].x = room_cx[0] + i;	// spawn side by side in room 0
		players[i].y = room_cy[0];
		players[i].hpmax = players[i].hp = 20;
		players[i].atk = 4;
		players[i].def = 1;
		players[i].weapon_bonus = 0;
		players[i].armour_bonus = 0;
		players[i].potions = 0;
		players[i].scrolls = 0;
		players[i].gold = 0;
		players[i].alive = 1;
	}
	active_player = 0;
}

// Teleport a player to a random room's floor tile (used by the teleport scroll).
void teleport_player(byte pi) {
	byte r;
	r = rnd_range(0, num_rooms - 1);
	players[pi].x = room_cx[r];
	players[pi].y = room_cy[r];
}

void use_potion(byte pi) {
	if (players[pi].potions == 0) {
		set_msg("Kein Trank!         ");
		return;
	}
	players[pi].potions--;
	players[pi].hp += item_defs[IT_POT_HEAL].bonus;
	if (players[pi].hp > players[pi].hpmax) players[pi].hp = players[pi].hpmax;
	set_msg("Geheilt!            ");
}

void use_scroll(byte pi) {
	if (players[pi].scrolls == 0) {
		set_msg("Keine Schriftrolle! ");
		return;
	}
	players[pi].scrolls--;
	teleport_player(pi);
	set_msg("Teleportiert!       ");
}


// ---- Combat (bump attack, turn-based) --------------------------------------

// Score is weighted by monster danger (hp + 2x atk) instead of a flat
// per-kill value, so tougher monsters are worth more for the high score.
void player_attacks_monster(byte pi, byte mi) {
	int dmg;
	byte type;

	type = monsters[mi].type;
	dmg = (int)player_atk(pi) - (int)monster_defs[type].def / 2;
	if (dmg < 1) dmg = 1;
	if (dmg >= (int)monsters[mi].hp) {
		monsters[mi].hp = 0;
		monsters[mi].alive = 0;
		mapmonster[monsters[mi].y][monsters[mi].x] = NO_MONSTER;
		score += 10 + monster_defs[type].hpmax + monster_defs[type].atk * 2;
		msg_clear();
		msg_append(monster_names[type]);
		msg_append(" besiegt!");
	} else {
		monsters[mi].hp = (byte)((int)monsters[mi].hp - dmg);
		msg_clear();
		msg_append(monster_names[type]);
		msg_append(" HP:");
		msg_append_num(monsters[mi].hp);
		msg_append("/");
		msg_append_num(monster_defs[type].hpmax);
	}
}

void monster_attacks_player(byte mi, byte pi) {
	int dmg;
	dmg = (int)monster_defs[monsters[mi].type].atk - (int)player_def(pi) / 2;
	if (dmg < 1) dmg = 1;
	if (dmg >= players[pi].hp) {
		players[pi].hp = 0;
		players[pi].alive = 0;
	} else {
		players[pi].hp = (int)players[pi].hp - dmg;
	}
}


// ---- Turn logic -------------------------------------------------------

void try_move(byte pi, char key) {
	byte nx, ny, mi, oi;

	nx = players[pi].x;
	ny = players[pi].y;
	switch (key) {
		case 'w': case 'W': ny--; break;
		case 's': case 'S': ny++; break;
		case 'a': case 'A': nx--; break;
		case 'd': case 'D': nx++; break;
		default: return;	// unknown key: no turn spent by caller
	}

	if (terrain[ny][nx] == T_WALL) return;

	mi = monster_at(nx, ny);
	if (mi != NO_MONSTER) {
		player_attacks_monster(pi, mi);
		return;
	}

	oi = player_at(nx, ny, pi);
	if (oi != 0xff) {
		set_msg("Blockiert!          ");
		return;
	}

	players[pi].x = nx;
	players[pi].y = ny;

	oi = gold_at(nx, ny);
	if (oi != 0xff) {
		gold_alive[oi] = 0;
		players[pi].gold += 10;
		set_msg("Gold gefunden!      ");
	}

	oi = item_at(nx, ny);
	if (oi != 0xff) {
		item_alive[oi] = 0;
		switch (item_defs[item_type[oi]].kind) {
			case IK_WEAPON:
				if (item_defs[item_type[oi]].bonus > players[pi].weapon_bonus)
					players[pi].weapon_bonus = item_defs[item_type[oi]].bonus;
				set_msg("Neue Waffe!         ");
				break;
			case IK_ARMOUR:
				if (item_defs[item_type[oi]].bonus > players[pi].armour_bonus)
					players[pi].armour_bonus = item_defs[item_type[oi]].bonus;
				set_msg("Neue Ruestung!      ");
				break;
			case IK_POTION:
				if (players[pi].potions < 9) players[pi].potions++;
				set_msg("Trank eingesammelt! ");
				break;
			case IK_SCROLL:
				if (players[pi].scrolls < 9) players[pi].scrolls++;
				set_msg("Schriftrolle da!    ");
				break;
		}
	}

	if (terrain[ny][nx] == T_STAIRS) {
		set_msg("Treppe... (Ziel!)   ");
	}
}

// Very small monster AI: step toward the nearest player if close, else idle.
void monsters_turn(void) {
	byte i, pi, best_pi;
	int dx, dy, best_dist, d;
	byte nx, ny;

	for (i = 0; i < num_monsters; i++) {
		if (!monsters[i].alive) continue;

		best_pi = 0xff;
		best_dist = 999;
		for (pi = 0; pi < NUM_PLAYERS; pi++) {
			if (!players[pi].alive) continue;
			dx = (int)players[pi].x - (int)monsters[i].x;
			dy = (int)players[pi].y - (int)monsters[i].y;
			if (dx < 0) dx = -dx;
			if (dy < 0) dy = -dy;
			d = dx + dy;
			if (d < best_dist) { best_dist = d; best_pi = pi; }
		}
		if (best_pi == 0xff || best_dist > 6) continue;	// too far: idle

		nx = monsters[i].x;
		ny = monsters[i].y;
		if (players[best_pi].x > nx) nx++;
		else if (players[best_pi].x < nx) nx--;
		else if (players[best_pi].y > ny) ny++;
		else if (players[best_pi].y < ny) ny--;

		if (nx == players[best_pi].x && ny == players[best_pi].y) {
			monster_attacks_player(i, best_pi);
			continue;
		}
		if (terrain[ny][nx] == T_WALL) continue;
		if (monster_at(nx, ny) != NO_MONSTER) continue;
		if (player_at(nx, ny, 0xff) != 0xff) continue;

		mapmonster[monsters[i].y][monsters[i].x] = NO_MONSTER;
		monsters[i].x = nx;
		monsters[i].y = ny;
		mapmonster[ny][nx] = i;
	}
}


// ---- Rendering --------------------------------------------------------

const byte *player_sprite(byte pi) {
	return (pi == 0) ? spr_player1 : spr_player2;
}

void render(void) {
	byte i, j;
	int wx, wy;
	byte mi, gi, oi;
	byte cx, cy;

	cx = players[active_player].x;
	cy = players[active_player].y;

	for (i = 0; i < VIEW_H; i++) {
		wy = (int)cy + i - (VIEW_H / 2);
		for (j = 0; j < VIEW_W; j++) {
			wx = (int)cx + j - (VIEW_W / 2);

			if (wx < 0 || wy < 0 || wx >= DUN_W || wy >= DUN_H) {
				draw_tile(j, i, spr_wall);
				continue;
			}

			oi = player_at((byte)wx, (byte)wy, 0xff);
			if (oi != 0xff) {
				draw_tile(j, i, player_sprite(oi));
				continue;
			}
			mi = mapmonster[wy][wx];
			if (mi != NO_MONSTER && monsters[mi].alive) {
				draw_tile(j, i, monster_defs[monsters[mi].type].sprite);
				continue;
			}
			gi = gold_at((byte)wx, (byte)wy);
			if (gi != 0xff) {
				draw_tile(j, i, spr_gold);
				continue;
			}
			gi = item_at((byte)wx, (byte)wy);
			if (gi != 0xff) {
				draw_tile(j, i, item_defs[item_type[gi]].sprite);
				continue;
			}
			draw_tile(j, i, terrain_sprite(terrain[wy][wx]));
		}
	}

	print_text(0, 0, "HP:00/00 ATK:00 DEF:00 P:0 S:0 SC:0000");
	print_num2(3, 0, (byte)players[active_player].hp);
	print_num2(6, 0, (byte)players[active_player].hpmax);
	print_num2(13, 0, player_atk(active_player));
	print_num2(20, 0, player_def(active_player));
	lcd_putchar_at(25, 0, '0' + players[active_player].potions);
	lcd_putchar_at(29, 0, '0' + players[active_player].scrolls);
	print_num4(34, 0, score);
	print_line(1, msg);
}


// ---- Main loop ----------------------------------------------------------

void next_player_turn(void) {
	byte tries;
	tries = 0;
	do {
		active_player = (active_player + 1) % NUM_PLAYERS;
		tries++;
	} while (!players[active_player].alive && tries <= NUM_PLAYERS);
}

byte any_player_alive(void) {
	byte i;
	for (i = 0; i < NUM_PLAYERS; i++) {
		if (players[i].alive) return 1;
	}
	return 0;
}

// (Re-)generates a fresh dungeon and resets player/score state. rng_seed is
// NOT reset here, only once before the very first call, so a restart after
// Game Over gets a different layout instead of repeating the same one.
void start_game(void) {
	generate_dungeon();
	populate();
	init_players();
	score = 0;
	set_msg("Auf geht's!");
}

void main(void) {
	char key;

	lcd_clear();
	rng_seed = 0xACE1u;	// TODO: seed from a free-running timer/keyboard jitter
	start_game();

	while (1) {
		render();

		if (!any_player_alive()) {
			if (score > high_score) high_score = score;
			msg_clear();
			msg_append("GAME OVER! HI:");
			msg_append_num(high_score);
			print_line(1, msg);

			key = keyboard_getchar();
			if (key == 'x' || key == 'X') break;
			lcd_clear();
			start_game();
			continue;
		}

		key = keyboard_getchar();
		if (key == 'x' || key == 'X') break;

		switch (key) {
			case 'u': case 'U': use_potion(active_player); break;
			case 'r': case 'R': use_scroll(active_player); break;
			default: try_move(active_player, key); break;
		}

		if (NUM_PLAYERS == 1) {
			monsters_turn();
		} else {
			next_player_turn();
			if (active_player == 0) {
				// once every player has had a turn, let the monsters go
				monsters_turn();
			}
		}
	}
}

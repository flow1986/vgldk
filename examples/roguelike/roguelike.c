/*
	A tiny sprite-based Rogue-like for the VGLDK (GL6000SL)

	- Real 8x8 bitmap sprites (no ASCII tiles) for player, monsters and walls
	- Turn-based movement: player moves one tile (WASD), then every monster
	  moves one step along its own fixed patrol path
	- Bumping into a monster triggers a very simplified turn-based fight
*/

#include <vgldk.h>
#include <stdiomin.h>


// ---- Grid layout --------------------------------------------------------

#define TILE 8	// sprite size in pixels. Must stay 8: draw_tile() relies on
				// tiles being exactly one byte wide in the framebuffer.

#define MAP_W 24
#define MAP_H 8

#define GRID_X 8	// pixel origin of the grid (must be a multiple of 8!)
#define GRID_Y 16	// pixel origin (leaves 2 text rows on top for the HUD)

#define T_FLOOR 0
#define T_WALL 1

const byte map[MAP_H][MAP_W] = {
	{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
	{1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
	{1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,1},
	{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
	{1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1},
	{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
	{1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1},
	{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};


// ---- 8x8 sprites (1 bit per pixel, MSB = leftmost pixel) ----------------

const byte sprite_floor[TILE] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const byte sprite_wall[TILE] = {
	0xff, 0x81, 0xbd, 0xa5, 0xa5, 0xbd, 0x81, 0xff,
};

const byte sprite_player[TILE] = {
	0x3c, 0x7e, 0x5a, 0x7e, 0x3c, 0x18, 0x3c, 0x66,
};

const byte sprite_monster_slime[TILE] = {
	0x00, 0x3c, 0x7e, 0xdb, 0xff, 0xff, 0xbd, 0x00,
};

const byte sprite_monster_bat[TILE] = {
	0x42, 0xe7, 0xff, 0x7e, 0x3c, 0x5a, 0x81, 0x00,
};

const byte sprite_monster_ghost[TILE] = {
	0x3c, 0x7e, 0xdb, 0xff, 0xff, 0xff, 0xbd, 0xa5,
};


// ---- Directions ----------------------------------------------------------

#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3
#define DIR_NONE 0xff


// ---- Monsters --------------------------------------------------------

#define NUM_MONSTERS 3
#define NO_MONSTER 0xff

typedef struct {
	byte x, y;
	byte hp, hpmax;
	byte atk;
	byte alive;
	const byte *path;
	byte path_len;
	byte path_pos;
	const byte *sprite;
} t_monster;

// Predefined patrol paths (simple back-and-forth patterns)
const byte path_a[] = {
	DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT,
	DIR_LEFT,  DIR_LEFT,  DIR_LEFT,  DIR_LEFT,  DIR_LEFT,  DIR_LEFT,  DIR_LEFT,  DIR_LEFT,
};
const byte path_b[] = {
	DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT,
	DIR_LEFT,  DIR_LEFT,  DIR_LEFT,  DIR_LEFT,  DIR_LEFT,
};
const byte path_c[] = {
	DIR_DOWN, DIR_DOWN, DIR_DOWN, DIR_DOWN, DIR_DOWN,
	DIR_UP,   DIR_UP,   DIR_UP,   DIR_UP,   DIR_UP,
};

t_monster monsters[NUM_MONSTERS];


// ---- Player ------------------------------------------------------------

byte player_x, player_y;
byte player_hp, player_hpmax;
byte player_atk;


// ---- Low level drawing --------------------------------------------------

// Blit an 8x8 sprite at tile coordinates (tx,ty). Relies on GRID_X being
// byte-aligned so every tile is exactly one framebuffer byte wide - no
// bit-shifting needed (unlike lcd_draw_glypth_at() for arbitrary text x).
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

void print_byte2(byte col, byte row, byte v) {
	lcd_putchar_at(col,   row, '0' + (v / 10));
	lcd_putchar_at(col+1, row, '0' + (v % 10));
}


// ---- Game logic --------------------------------------------------------

byte monster_at(byte x, byte y) {
	byte i;
	for (i = 0; i < NUM_MONSTERS; i++) {
		if (monsters[i].alive && monsters[i].x == x && monsters[i].y == y)
			return i;
	}
	return NO_MONSTER;
}

// Compute the tile that lies in direction "dir" from (x,y). Never called
// with a direction that would step outside the map (border is all walls).
void step_dir(byte x, byte y, byte dir, byte *nx, byte *ny) {
	*nx = x;
	*ny = y;
	switch (dir) {
		case DIR_UP:    (*ny)--; break;
		case DIR_DOWN:  (*ny)++; break;
		case DIR_LEFT:  (*nx)--; break;
		case DIR_RIGHT: (*nx)++; break;
	}
}

// Very simplified turn-based fight: player hits first: monster dies -> no
// counter-attack this round; otherwise the monster immediately hits back.
void fight(byte idx) {
	int dmg;

	dmg = (int)monsters[idx].hp - (int)player_atk;
	if (dmg <= 0) {
		monsters[idx].hp = 0;
		monsters[idx].alive = 0;
		print_text(0, 1, "Monster besiegt!   ");
	} else {
		monsters[idx].hp = (byte)dmg;

		dmg = (int)player_hp - (int)monsters[idx].atk;
		if (dmg <= 0) {
			player_hp = 0;
		} else {
			player_hp = (byte)dmg;
		}
		print_text(0, 1, "Kampf!             ");
	}
}

void player_turn(byte key) {
	byte dir;
	byte nx, ny;
	byte mi;

	switch (key) {
		case 'w': case 'W': dir = DIR_UP; break;
		case 's': case 'S': dir = DIR_DOWN; break;
		case 'a': case 'A': dir = DIR_LEFT; break;
		case 'd': case 'D': dir = DIR_RIGHT; break;
		default: dir = DIR_NONE; break;
	}
	if (dir == DIR_NONE) return;

	step_dir(player_x, player_y, dir, &nx, &ny);
	if (map[ny][nx] == T_WALL) return;

	mi = monster_at(nx, ny);
	if (mi != NO_MONSTER) {
		fight(mi);
	} else {
		player_x = nx;
		player_y = ny;
	}
}

void monsters_turn(void) {
	byte i;
	byte nx, ny, dir;

	for (i = 0; i < NUM_MONSTERS; i++) {
		if (!monsters[i].alive) continue;

		dir = monsters[i].path[monsters[i].path_pos];
		step_dir(monsters[i].x, monsters[i].y, dir, &nx, &ny);

		// Don't walk into the player (no monster-initiated attack, keep it simple)
		// and don't walk into walls or other monsters.
		if (nx == player_x && ny == player_y) {
			// wait this turn
		} else if (map[ny][nx] == T_WALL) {
			// wait this turn
		} else if (monster_at(nx, ny) != NO_MONSTER) {
			// wait this turn
		} else {
			monsters[i].x = nx;
			monsters[i].y = ny;
		}

		monsters[i].path_pos++;
		if (monsters[i].path_pos >= monsters[i].path_len)
			monsters[i].path_pos = 0;
	}
}

byte monsters_left(void) {
	byte i, n;
	n = 0;
	for (i = 0; i < NUM_MONSTERS; i++) {
		if (monsters[i].alive) n++;
	}
	return n;
}

void render(void) {
	byte x, y;
	byte i;

	for (y = 0; y < MAP_H; y++) {
		for (x = 0; x < MAP_W; x++) {
			draw_tile(x, y, (map[y][x] == T_WALL) ? sprite_wall : sprite_floor);
		}
	}

	for (i = 0; i < NUM_MONSTERS; i++) {
		if (monsters[i].alive)
			draw_tile(monsters[i].x, monsters[i].y, monsters[i].sprite);
	}

	draw_tile(player_x, player_y, sprite_player);

	print_text(0, 0, "HP:      Monster:  ");
	print_byte2(3, 0, player_hp);
	print_byte2(19, 0, monsters_left());
}

void init_game(void) {
	player_x = 2; player_y = 3;
	player_hp = 20; player_hpmax = 20;
	player_atk = 4;

	monsters[0].x = 10; monsters[0].y = 3;
	monsters[0].hp = monsters[0].hpmax = 6;
	monsters[0].atk = 2;
	monsters[0].alive = 1;
	monsters[0].path = path_a;
	monsters[0].path_len = sizeof(path_a);
	monsters[0].path_pos = 0;
	monsters[0].sprite = sprite_monster_slime;

	monsters[1].x = 15; monsters[1].y = 5;
	monsters[1].hp = monsters[1].hpmax = 8;
	monsters[1].atk = 3;
	monsters[1].alive = 1;
	monsters[1].path = path_b;
	monsters[1].path_len = sizeof(path_b);
	monsters[1].path_pos = 0;
	monsters[1].sprite = sprite_monster_bat;

	monsters[2].x = 20; monsters[2].y = 1;
	monsters[2].hp = monsters[2].hpmax = 10;
	monsters[2].atk = 4;
	monsters[2].alive = 1;
	monsters[2].path = path_c;
	monsters[2].path_len = sizeof(path_c);
	monsters[2].path_pos = 0;
	monsters[2].sprite = sprite_monster_ghost;
}

void main() {
	byte key;

	lcd_clear();
	init_game();

	while (1) {
		render();

		if (player_hp == 0) {
			print_text(0, 1, "GAME OVER!         ");
			while (1) { }
		}
		if (monsters_left() == 0) {
			print_text(0, 1, "GEWONNEN!          ");
			while (1) { }
		}

		key = keyboard_getchar();
		player_turn(key);

		if (player_hp == 0) continue;	// counter-attack killed the player, show it next frame

		monsters_turn();
	}
}

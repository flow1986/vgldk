/*
	A tiny sprite-based Pac-Man clone for the VGLDK (GL6000SL)

	- Real 8x8 bitmap sprites (no ASCII tiles) for Pac-Man, ghosts, walls and dots
	- Real-time movement: the game runs on a free-running tick loop, not on
	  blocking key presses. WASD is polled every tick via the "currently
	  held" scancode list (keyboard_pressed[]/KEY_CODES[]), so Pac-Man keeps
	  gliding in the last chosen direction (like the original) instead of
	  requiring one keypress per tile.
	- A symmetric maze with a central ghost house (with exit) and left/right
	  wrap-around tunnels, closer to the original Pac-Man layout.
	- Eat all dots to win. Touching a ghost ends the game.
*/

#include <vgldk.h>
#include <stdiomin.h>


// ---- Grid layout --------------------------------------------------------

#define TILE 8	// sprite size in pixels. Must stay 8: draw_tile() relies on
				// tiles being exactly one byte wide in the framebuffer.

#define MAP_W 30	// 30*8 = 240 = full LCD width
#define MAP_H 11	// 11*8 = 88, leaves an 8px HUD strip on top

#define GRID_X 0	// pixel origin of the grid (must be a multiple of 8!)
#define GRID_Y 8	// pixel origin (leaves 1 text row on top for the HUD)

#define T_FLOOR 0
#define T_WALL 1

#define TUNNEL_ROW 5	// row that wraps around left/right, like the original side tunnels

// A maze closer to the original: outer walls, symmetric pillar blocks,
// a central ghost house (row4 = solid top, row5 = interior, row6 = bottom
// with a 2-tile exit) and a tunnel row that wraps around at the edges.
const byte map[MAP_H][MAP_W] = {
	{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
	{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
	{1,0,0,1,1,1,0,0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0,0,1,1,1,0,0,1},
	{1,0,0,1,1,1,0,0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0,0,1,1,1,0,0,1},
	{1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1},
	{0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0},
	{1,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,1},
	{1,0,0,1,1,1,0,0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0,0,1,1,1,0,0,1},
	{1,0,0,1,1,1,0,0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0,0,1,1,1,0,0,1},
	{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
	{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

byte dots[MAP_H][MAP_W];	// 1 = dot still there, 0 = eaten/none
byte dots_left;


// ---- 8x8 sprites (1 bit per pixel, MSB = leftmost pixel) ----------------

const byte sprite_floor[TILE] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const byte sprite_wall[TILE] = {
	0xff, 0x81, 0xbd, 0xa5, 0xa5, 0xbd, 0x81, 0xff,
};

const byte sprite_dot[TILE] = {
	0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00,
};

const byte sprite_pellet[TILE] = {
	0x00, 0x18, 0x3c, 0x7e, 0x7e, 0x3c, 0x18, 0x00,
};

const byte sprite_pacman_right[TILE] = {
	0x3c, 0x7e, 0xfc, 0xf0, 0xfc, 0x7e, 0x3c, 0x00,
};
const byte sprite_pacman_left[TILE] = {
	0x3c, 0x7e, 0x3f, 0x0f, 0x3f, 0x7e, 0x3c, 0x00,
};

const byte sprite_ghost1[TILE] = {
	0x3c, 0x7e, 0xff, 0xdb, 0xff, 0xff, 0xff, 0xaa,
};
const byte sprite_ghost2[TILE] = {
	0x3c, 0x7e, 0xff, 0xe7, 0xff, 0xff, 0xff, 0xaa,
};
const byte sprite_ghost3[TILE] = {
	0x3c, 0x7e, 0xff, 0x99, 0xff, 0xff, 0xff, 0xaa,
};
const byte sprite_ghost_frozen[TILE] = {
	0x3c, 0x7e, 0xdb, 0xff, 0xdb, 0xff, 0xdb, 0xaa,
};


// ---- Directions ----------------------------------------------------------

#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3
#define DIR_NONE 0xff


// ---- Timing ---------------------------------------------------------------

// How many main-loop ticks make up one tile-step. Tune these if the game
// feels too fast/slow on real hardware vs. MAME.
#define FRAME_DELAY 3000		// busy-wait length per tick (see delay())
#define PLAYER_TICKS 3		// Pac-Man moves one tile every N ticks
#define GHOST_TICKS 4		// ghosts move one tile every M ticks (slower than Pac-Man)
#define FREEZE_TICKS 60		// how many main-loop ticks a power pellet freezes the ghosts for
#define STARTING_LIVES 3

void delay(word n) {
	word i;
	for (i = 0; i < n; i++) {
		__asm
			nop
			nop
			nop
			nop
		__endasm;
	}
}


// ---- Ghosts --------------------------------------------------------------

#define NUM_GHOSTS 3
#define NO_GHOST 0xff

typedef struct {
	byte x, y;
	const byte *sprite;
} t_ghost;

t_ghost ghosts[NUM_GHOSTS];

// Previous actor positions used by the incremental renderer.
byte render_ready;
byte old_player_x, old_player_y;
byte old_ghost_x[NUM_GHOSTS];
byte old_ghost_y[NUM_GHOSTS];
byte old_frozen;
byte old_dots_left;
byte old_player_lives;

// Home tiles inside the ghost house, used both for the initial setup and to
// send a ghost back "home" after Pac-Man eats it while it's frozen.
const byte ghost_home_x[NUM_GHOSTS] = {13, 14, 16};
const byte ghost_home_y[NUM_GHOSTS] = {5, 5, 5};

word freeze_timer;	// >0 while ghosts are frozen (power pellet active)


// ---- Power pellets ---------------------------------------------------------

#define NUM_PELLETS 4
#define NO_PELLET 0xff

// One in each corner, like the original
const byte pellet_x[NUM_PELLETS] = {2, 27, 2, 27};
const byte pellet_y[NUM_PELLETS] = {1, 1, 9, 9};
byte pellet_alive[NUM_PELLETS];


// ---- Player (Pac-Man) ----------------------------------------------------

byte player_x, player_y;
byte heading;		// current movement direction, kept until blocked or changed
byte facing;		// DIR_LEFT or DIR_RIGHT - which way Pac-Man's sprite looks
byte player_lives;


// ---- Low level drawing ----------------------------------------------------

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

void print_byte1(byte col, byte row, byte v) {
	lcd_putchar_at(col, row, '0' + v);
}


// ---- Input (real-time, level-triggered) -----------------------------------

// Is the key with charcode "c" currently held down? Uses the live
// keyboard_pressed[] scancode list (updated by keyboard_update()) instead
// of the keyboard_getchar()/inkey() buffer, which only fires once per press
// (no typematic repeat) and is therefore unsuitable for continuous movement.
byte is_held(char c) {
	byte i;
	for (i = 0; i < keyboard_num_pressed; i++) {
		if (KEY_CODES[keyboard_pressed[i]] == c) return 1;
	}
	return 0;
}

void read_input(void) {
	if (is_held('w') || is_held('W')) heading = DIR_UP;
	else if (is_held('s') || is_held('S')) heading = DIR_DOWN;
	else if (is_held('a') || is_held('A')) heading = DIR_LEFT;
	else if (is_held('d') || is_held('D')) heading = DIR_RIGHT;
	// else: keep gliding in the last heading, like the original Pac-Man

	// Only left/right have their own sprite - up/down keep the last one
	if (heading == DIR_LEFT || heading == DIR_RIGHT) facing = heading;
}

// Blocks until ENTER has been freshly pressed and released again (ignores
// a key that was already held when we got here, and debounces the release
// so the menu can't immediately re-trigger on the next screen).
void wait_for_enter(void) {
	while (is_held(KEY_ENTER))  { delay(FRAME_DELAY); keyboard_update(); }
	while (!is_held(KEY_ENTER)) { delay(FRAME_DELAY); keyboard_update(); }
	while (is_held(KEY_ENTER))  { delay(FRAME_DELAY); keyboard_update(); }
}

void show_game_over_menu(const char *title) {
	print_text(0, 1, title);
	print_text(0, 2, "ENTER = Neustart");
	wait_for_enter();
}


// ---- Game logic -----------------------------------------------------------

byte ghost_at(byte x, byte y) {
	byte i;
	for (i = 0; i < NUM_GHOSTS; i++) {
		if (ghosts[i].x == x && ghosts[i].y == y) return i;
	}
	return NO_GHOST;
}

byte pellet_at(byte x, byte y) {
	byte i;
	for (i = 0; i < NUM_PELLETS; i++) {
		if (pellet_alive[i] && pellet_x[i] == x && pellet_y[i] == y) return i;
	}
	return NO_PELLET;
}

// Compute the tile that lies in direction "dir" from (x,y). Returns 0 if
// that step would leave the map (only possible on the tunnel row, which
// wraps around left/right instead of being blocked).
byte step_dir(byte x, byte y, byte dir, byte *nx, byte *ny) {
	*nx = x;
	*ny = y;
	switch (dir) {
		case DIR_UP:
			if (y == 0) return 0;
			(*ny)--;
			break;
		case DIR_DOWN:
			if (y == MAP_H - 1) return 0;
			(*ny)++;
			break;
		case DIR_LEFT:
			if (x == 0) {
				if (y != TUNNEL_ROW) return 0;
				*nx = MAP_W - 1;
			} else {
				(*nx)--;
			}
			break;
		case DIR_RIGHT:
			if (x == MAP_W - 1) {
				if (y != TUNNEL_ROW) return 0;
				*nx = 0;
			} else {
				(*nx)++;
			}
			break;
	}
	return 1;
}

// Send a ghost back into the house after Pac-Man ate it while it was frozen.
void ghost_respawn(byte idx) {
	ghosts[idx].x = ghost_home_x[idx];
	ghosts[idx].y = ghost_home_y[idx];
}

// Try to advance Pac-Man by one tile in the current heading (called once
// every PLAYER_TICKS ticks - see main()).
void player_tick(void) {
	byte nx, ny;
	byte pi;

	if (heading == DIR_NONE) return;
	if (!step_dir(player_x, player_y, heading, &nx, &ny)) return;
	if (map[ny][nx] == T_WALL) return;

	player_x = nx;
	player_y = ny;

	if (dots[player_y][player_x]) {
		dots[player_y][player_x] = 0;
		dots_left--;
	}

	pi = pellet_at(player_x, player_y);
	if (pi != NO_PELLET) {
		pellet_alive[pi] = 0;
		freeze_timer = FREEZE_TICKS;
	}
}

// Try to step a single ghost into direction "dir". Fails (returns 0) on
// walls or another ghost occupying the target tile.
byte try_ghost_move(byte idx, byte dir) {
	byte nx, ny;

	if (dir == DIR_NONE) return 0;
	if (!step_dir(ghosts[idx].x, ghosts[idx].y, dir, &nx, &ny)) return 0;
	if (map[ny][nx] == T_WALL) return 0;
	if (ghost_at(nx, ny) != NO_GHOST) return 0;

	ghosts[idx].x = nx;
	ghosts[idx].y = ny;
	return 1;
}

// Very simple chase AI: try to close the bigger of the two axis distances
// to Pac-Man first, then the other axis, then just give up (wait).
void ghost_tick(byte idx) {
	int dx, dy;
	int adx, ady;
	byte dir_x, dir_y;

	dx = (int)player_x - (int)ghosts[idx].x;
	dy = (int)player_y - (int)ghosts[idx].y;

	dir_x = (dx < 0) ? DIR_LEFT : (dx > 0) ? DIR_RIGHT : DIR_NONE;
	dir_y = (dy < 0) ? DIR_UP   : (dy > 0) ? DIR_DOWN  : DIR_NONE;

	adx = (dx < 0) ? -dx : dx;
	ady = (dy < 0) ? -dy : dy;

	if (ady > adx) {
		if (try_ghost_move(idx, dir_y)) return;
		if (try_ghost_move(idx, dir_x)) return;
	} else {
		if (try_ghost_move(idx, dir_x)) return;
		if (try_ghost_move(idx, dir_y)) return;
	}
	// stuck behind a wall/other ghost this tick - just wait
}

void ghosts_tick(void) {
	byte i;
	for (i = 0; i < NUM_GHOSTS; i++) {
		ghost_tick(i);
	}
}

void render_cell(byte x, byte y) {
	byte i;

	if (map[y][x] == T_WALL) draw_tile(x, y, sprite_wall);
	else if (dots[y][x]) draw_tile(x, y, sprite_dot);
	else draw_tile(x, y, sprite_floor);

	for (i = 0; i < NUM_PELLETS; i++) {
		if (pellet_alive[i] && pellet_x[i] == x && pellet_y[i] == y)
			draw_tile(x, y, sprite_pellet);
	}

	for (i = 0; i < NUM_GHOSTS; i++) {
		if (ghosts[i].x == x && ghosts[i].y == y)
			draw_tile(x, y, (freeze_timer > 0) ? sprite_ghost_frozen : ghosts[i].sprite);
	}

	if (player_x == x && player_y == y)
		draw_tile(x, y, (facing == DIR_LEFT) ? sprite_pacman_left : sprite_pacman_right);
}

void render(void) {
	byte x, y;
	byte i;
	byte frozen;
	byte first_render;

	frozen = (freeze_timer > 0);
	first_render = !render_ready;
	if (first_render) {
		for (y = 0; y < MAP_H; y++)
			for (x = 0; x < MAP_W; x++)
				render_cell(x, y);
		render_ready = 1;
	} else {
		// Redraw only cells affected by actor movement or a sprite-state change.
		render_cell(old_player_x, old_player_y);
		render_cell(player_x, player_y);
		for (i = 0; i < NUM_GHOSTS; i++) {
			render_cell(old_ghost_x[i], old_ghost_y[i]);
			render_cell(ghosts[i].x, ghosts[i].y);
		}
		if (frozen != old_frozen) {
			for (i = 0; i < NUM_GHOSTS; i++)
				render_cell(ghosts[i].x, ghosts[i].y);
		}
	}

	if (first_render || dots_left != old_dots_left || player_lives != old_player_lives) {
		print_text(0, 0, "Dots:      Lives: ");
		print_byte2(5, 0, dots_left);
		print_byte1(18, 0, player_lives);
	}

	old_player_x = player_x;
	old_player_y = player_y;
	for (i = 0; i < NUM_GHOSTS; i++) {
		old_ghost_x[i] = ghosts[i].x;
		old_ghost_y[i] = ghosts[i].y;
	}
	old_frozen = frozen;
	old_dots_left = dots_left;
	old_player_lives = player_lives;
}

void init_game(void) {
	byte x, y;

	player_x = 14; player_y = 9;
	heading = DIR_LEFT;
	facing = DIR_LEFT;
	player_lives = STARTING_LIVES;
	freeze_timer = 0;

	// Ghosts start inside the house interior (row 5, cols 13-16)
	ghosts[0].x = ghost_home_x[0]; ghosts[0].y = ghost_home_y[0]; ghosts[0].sprite = sprite_ghost1;
	ghosts[1].x = ghost_home_x[1]; ghosts[1].y = ghost_home_y[1]; ghosts[1].sprite = sprite_ghost2;
	ghosts[2].x = ghost_home_x[2]; ghosts[2].y = ghost_home_y[2]; ghosts[2].sprite = sprite_ghost3;

	for (x = 0; x < NUM_PELLETS; x++) pellet_alive[x] = 1;

	dots_left = 0;
	for (y = 0; y < MAP_H; y++) {
		for (x = 0; x < MAP_W; x++) {
			dots[y][x] = (map[y][x] == T_FLOOR) ? 1 : 0;
		}
	}

	// No dots inside the ghost house (interior + exit tiles)
	dots[5][13] = 0; dots[5][14] = 0; dots[5][15] = 0; dots[5][16] = 0;
	dots[6][14] = 0; dots[6][15] = 0;

	dots[player_y][player_x] = 0;

	// No dots underneath the power pellets
	for (x = 0; x < NUM_PELLETS; x++) dots[pellet_y[x]][pellet_x[x]] = 0;

	for (y = 0; y < MAP_H; y++) {
		for (x = 0; x < MAP_W; x++) {
			if (dots[y][x]) dots_left++;
		}
	}
}

// Pac-Man got caught by a ghost while not frozen: lose a life and reset
// everyone's position (dots/pellets already eaten stay eaten).
void respawn_after_hit(void) {
	byte i;

	player_x = 14; player_y = 9;
	heading = DIR_LEFT;
	facing = DIR_LEFT;
	freeze_timer = 0;

	for (i = 0; i < NUM_GHOSTS; i++) ghost_respawn(i);
}

void main() {
	word tick;
	byte gi;

	lcd_clear();
	init_game();
	render_ready = 0;
	tick = 0;

	while (1) {
		delay(FRAME_DELAY);
		keyboard_update();
		read_input();

		tick++;

		if (freeze_timer > 0) freeze_timer--;

		if ((tick % PLAYER_TICKS) == 0) {
			player_tick();
		}
		if (freeze_timer == 0 && (tick % GHOST_TICKS) == 0) {
			ghosts_tick();
		}

		gi = ghost_at(player_x, player_y);
		if (gi != NO_GHOST) {
			if (freeze_timer > 0) {
				// Frozen ghost gets eaten - back to the house, no harm done
				ghost_respawn(gi);
			} else {
				player_lives--;
				if (player_lives == 0) {
					render();
					show_game_over_menu("GAME OVER!");
					lcd_clear();
					init_game();
					render_ready = 0;
					tick = 0;
					continue;
				}
				respawn_after_hit();
			}
		}

		render();

		if (dots_left == 0) {
			show_game_over_menu("GEWONNEN!");
			lcd_clear();
			init_game();
			render_ready = 0;
			tick = 0;
			continue;
		}
	}
}

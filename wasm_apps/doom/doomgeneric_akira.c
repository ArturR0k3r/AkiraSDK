/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * doomgeneric backend for AkiraOS (AkiraConsole, 320x240 panel).
 *
 * Video: Doom draws 320x200 palette indices (CMAP256); each frame is
 * converted to RGB565 and sent with display_raw_write_async(),
 * centred vertically (20-pixel black bars).
 *
 * Controls:
 *   D-pad up/down       forward / back (menu cursor)
 *   D-pad left/right    turn (menu value); with Y held: strafe
 *   A                   fire; menu confirm
 *   B                   use / open; menu back
 *   X                   next weapon;    Y+X: previous weapon
 *   Y+A                 automap
 *   HOME tap            menu (Esc). HOME hold returns to AkiraOS (the OS
 *                       does that), so the tap fires on release only.
 *   Run is always on.
 */
#include <stdint.h>
#include <string.h>

#include "akira_natives.h"
#include "akira_console.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_video.h"

#define SCREEN_W   320
#define SCREEN_H   240
#define DOOM_Y     ((SCREEN_H - DOOMGENERIC_RESY) / 2)

#define BTN_HOME   (1u << 1) /* zephyr,code 1; no AKIRA_BTN_* for HOME */
#define HOME_LONG_MS 1000    /* CONFIG_AKIRA_HOME_BUTTON_GPIO_LONG_MS */

/* Keys for weapon cycling; bound at runtime (Doom leaves them unbound). */
#define KEY_NEXTWEAPON ']'
#define KEY_PREVWEAPON '['

extern int key_nextweapon, key_prevweapon, joybspeed;

static uint32_t now_ms(void);

static uint16_t frame[DOOMGENERIC_RESX * DOOMGENERIC_RESY];
static uint16_t pal565[256];
static int frame_in_flight;

/* ---- key event queue ---------------------------------------------------- */

#define KEYQ_LEN 32
static struct { uint8_t pressed, key; } keyq[KEYQ_LEN];
static unsigned keyq_head, keyq_tail;

static void key_event(int pressed, uint8_t key)
{
	if (keyq_head - keyq_tail < KEYQ_LEN) {
		unsigned i = keyq_head++ % KEYQ_LEN;

		keyq[i].pressed = (uint8_t)pressed;
		keyq[i].key = key;
	}
}

static void key_tap(uint8_t key)
{
	key_event(1, key);
	key_event(0, key);
}

/* Held Doom keys, one bit per logical control, so a release always goes to
 * the key that was pressed even if Y changed in between. */
enum { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_FIRE, K_USE, K_COUNT };
static uint8_t held_key[K_COUNT];

static void set_held(int k, uint8_t key)
{
	if (held_key[k] == key) {
		return;
	}
	if (held_key[k]) {
		key_event(0, held_key[k]);
	}
	if (key) {
		key_event(1, key);
	}
	held_key[k] = key;
}

static uint32_t prev_btns;
static uint32_t home_down_ms;
static int bindings_done;

static void poll_buttons(void)
{
	uint32_t b = (uint32_t)input_get_buttons();
	uint32_t pressed = b & ~prev_btns;
	int y = (b & AKIRA_BTN_Y) != 0;

	/* Config is loaded by now (first poll happens in the game loop):
	 * bind weapon cycling and force always-run ("joyb_speed 29" hack). */
	if (!bindings_done) {
		key_nextweapon = KEY_NEXTWEAPON;
		key_prevweapon = KEY_PREVWEAPON;
		joybspeed = 29;
		bindings_done = 1;
	}

	set_held(K_UP,    (b & AKIRA_BTN_UP)    ? KEY_UPARROW : 0);
	set_held(K_DOWN,  (b & AKIRA_BTN_DOWN)  ? KEY_DOWNARROW : 0);
	set_held(K_LEFT,  (b & AKIRA_BTN_LEFT)  ? (y ? KEY_STRAFE_L : KEY_LEFTARROW) : 0);
	set_held(K_RIGHT, (b & AKIRA_BTN_RIGHT) ? (y ? KEY_STRAFE_R : KEY_RIGHTARROW) : 0);
	set_held(K_FIRE,  ((b & AKIRA_BTN_A) && !y) ? KEY_FIRE : 0);
	set_held(K_USE,   (b & AKIRA_BTN_B) ? KEY_USE : 0);

	if (pressed & AKIRA_BTN_A) {
		if (y) {
			key_tap(KEY_TAB);   /* automap */
		} else {
			key_tap(KEY_ENTER); /* menu select */
			key_tap('y');       /* "are you sure?" prompts */
		}
	}
	if (pressed & AKIRA_BTN_B) {
		key_tap(KEY_BACKSPACE); /* menu back */
		key_tap('n');
	}
	if (pressed & AKIRA_BTN_X) {
		key_tap(y ? KEY_PREVWEAPON : KEY_NEXTWEAPON);
	}

	uint32_t now = now_ms();

	if (pressed & BTN_HOME) {
		home_down_ms = now;
	} else if ((prev_btns & BTN_HOME) && !(b & BTN_HOME) &&
		   (uint32_t)(now - home_down_ms) < HOME_LONG_MS) {
		key_tap(KEY_ESCAPE);
	}

	prev_btns = b;
}

/* ---- doomgeneric hooks --------------------------------------------------- */

void DG_Init(void)
{
	/* Black bars above and below the 200-line picture. */
	memset(frame, 0, sizeof(frame));
	display_raw_write(0, 0, SCREEN_W, DOOM_Y, frame, SCREEN_W * DOOM_Y * 2);
	display_raw_write(0, DOOM_Y + DOOMGENERIC_RESY, SCREEN_W, DOOM_Y, frame,
			  SCREEN_W * DOOM_Y * 2);
}

void DG_DrawFrame(void)
{
	const uint8_t *src = (const uint8_t *)DG_ScreenBuffer;

	if (palette_changed) {
		for (int i = 0; i < 256; i++) {
			uint16_t c = ((colors[i].r & 0xF8) << 8) |
				     ((colors[i].g & 0xFC) << 3) |
				     (colors[i].b >> 3);

			/* No byte swap: this panel reads the uint16 as is (checked
			 * with red/green/blue/white test blocks on AkiraConsole Prod). */
			pal565[i] = c;
		}
		palette_changed = 0;
	}

	/* The previous transfer reads this buffer until it completes. */
	if (frame_in_flight) {
		display_raw_wait();
	}
	for (int i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; i++) {
		frame[i] = pal565[src[i]];
	}
	frame_in_flight = display_raw_write_async(0, DOOM_Y, DOOMGENERIC_RESX,
						  DOOMGENERIC_RESY, frame,
						  sizeof(frame)) == 0;
}

void DG_SleepMs(uint32_t ms)
{
	delay(ms * 1000u);
}

/* Monotonic ms clock. The firmware uptime was seen stepping back ~18 s once;
 * Doom then runs no game tics until time catches up (the picture freezes
 * while frames keep being drawn). Never let the clock go backwards. */
static uint32_t now_ms(void)
{
	static uint32_t last;
	uint32_t t = (uint32_t)rtc_get_uptime_ms();

	if ((int32_t)(t - last) > 0) {
		last = t;
	}
	return last;
}

uint32_t DG_GetTicksMs(void)
{
	return now_ms();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
	if (keyq_head == keyq_tail) {
		poll_buttons();
	}
	if (keyq_head == keyq_tail) {
		return 0;
	}
	*pressed = keyq[keyq_tail % KEYQ_LEN].pressed;
	*key = keyq[keyq_tail % KEYQ_LEN].key;
	keyq_tail++;
	return 1;
}

void DG_SetWindowTitle(const char *title)
{
	(void)title;
}

/* The IWAD lives in the app's sandbox: /SD:/apps/doom/ on the SD card. */
static char *doom_argv[] = {
	"doom", "-iwad", "freedoom1.wad", "-kb", DOOM_ZONE_KB, NULL,
};

int main(void)
{
	doomgeneric_Create(5, doom_argv);
	for (;;) {
		doomgeneric_Tick();
	}
	return 0;
}

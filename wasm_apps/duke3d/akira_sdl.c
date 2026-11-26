/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The SDL subset Duke3D's Build display layer uses, on AkiraOS natives
 * (AkiraConsole, 320x240 panel). Replaces the ESP32 port's SDL_*.c and
 * spi_lcd.c.
 *
 * Video: Build draws 320x200 palette indices into one 8bpp surface; each
 * frame is converted to RGB565 and sent with display_raw_write_async(),
 * centred vertically (20-pixel black bars).
 *
 * Controls (keyMode is set by the menu code: 1 while a menu is open):
 *   In game                       In menus
 *   D-pad      move / turn        cursor
 *   Y+left/right  strafe
 *   A          fire               select (Enter)
 *   B          open / use         back (Esc)
 *   X          jump               "Y" (yes)
 *   Y+X        crouch
 *   Y+A        next weapon
 *   Y+B        use inventory item
 *   Y+up       map
 *   Y+down     next inventory item
 *   HOME tap   menu (Esc). HOME hold returns to AkiraOS (the OS does that),
 *              so the tap fires on release only.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "SDL.h"
#include "akira_natives.h"
#include "akira_console.h"

#define SCREEN_W   320
#define SCREEN_H   240
#define GAME_W     320
#define GAME_H     200
#define GAME_Y     ((SCREEN_H - GAME_H) / 2)

#define BTN_HOME     (1u << 1) /* zephyr,code 1; no AKIRA_BTN_* for HOME */
#define HOME_LONG_MS 1000      /* CONFIG_AKIRA_HOME_BUTTON_GPIO_LONG_MS */

int keyMode; /* 0 in game, 1 in menus; written by menues.c */
int16_t lcdpal[256];

static SDL_Surface screen;
static SDL_PixelFormat screen_fmt;
static uint8_t pixels[GAME_W * GAME_H] __attribute__((aligned(4)));
static uint16_t frame[GAME_W * GAME_H] __attribute__((aligned(4)));
static uint16_t pal565[256];
static int frame_in_flight;

/* ---- timing --------------------------------------------------------------- */

static uint32_t last_flip_ms;
static void stall_check(uint32_t raw, uint32_t now);

#ifdef DUKE_DIAG
/* Diagnostic build (make DIAG=1): the game is compiled with
 * -finstrument-functions; every function entry goes into a ring. If no frame
 * was drawn for 3 s, the ring (function table indices, oldest first) is
 * logged, naming the loop that spins without drawing. */
#define DIAG_RING 64
static uint32_t diag_ring[DIAG_RING];
static uint32_t diag_pos, diag_calls, diag_report_ms;

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *fn, void *site)
{
	(void)fn; (void)site;
}

__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *fn, void *site)
{
	(void)site;
	diag_ring[diag_pos++ % DIAG_RING] = (uint32_t)(uintptr_t)fn;
	if (++diag_calls % 4096) {
		return;
	}

	uint32_t now = (uint32_t)rtc_get_uptime_ms();

	if (!last_flip_ms || now - last_flip_ms < 3000 || now - diag_report_ms < 3000) {
		return;
	}
	diag_report_ms = now;
	printf("duke3d diag: no frame for %u ms, last functions:\n", now - last_flip_ms);
	for (int i = 0; i < DIAG_RING; i += 16) {
		uint32_t *r = diag_ring;
		uint32_t b = diag_pos + i;

		printf("diag %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
		       r[b % DIAG_RING], r[(b + 1) % DIAG_RING], r[(b + 2) % DIAG_RING],
		       r[(b + 3) % DIAG_RING], r[(b + 4) % DIAG_RING], r[(b + 5) % DIAG_RING],
		       r[(b + 6) % DIAG_RING], r[(b + 7) % DIAG_RING], r[(b + 8) % DIAG_RING],
		       r[(b + 9) % DIAG_RING], r[(b + 10) % DIAG_RING], r[(b + 11) % DIAG_RING],
		       r[(b + 12) % DIAG_RING], r[(b + 13) % DIAG_RING], r[(b + 14) % DIAG_RING],
		       r[(b + 15) % DIAG_RING]);
	}
}
#endif

/* Monotonic ms clock: the firmware uptime can step back (SMP timer skew). */
Uint32 SDL_GetTicks(void)
{
	static uint32_t last;
	uint32_t t = (uint32_t)rtc_get_uptime_ms();

	if ((int32_t)(t - last) > 0) {
		last = t;
	}
	stall_check(t, last);
	return last;
}

/* No frame for STALL_MS: log the game clocks and the move queue once per
 * STALL_MS, to tell a stuck clock from a loop that never draws. */
#define STALL_MS 3000
extern volatile int32_t totalclock;
extern int32_t ototalclock, lockclock, movefifoplc, movefifoend[];

static void stall_check(uint32_t raw, uint32_t now)
{
	static uint32_t last_report, calls;

	calls++;
	if (!last_flip_ms || now - last_flip_ms < STALL_MS || now - last_report < STALL_MS) {
		return;
	}
	last_report = now;
	printf("duke3d: no frame for %u ms: uptime %u clock %u, %u clock reads, "
	       "totalclock %d ototalclock %d lockclock %d, moves queued %d\n",
	       now - last_flip_ms, raw, now, calls, (int)totalclock, (int)ototalclock,
	       (int)lockclock, (int)(movefifoend[0] - movefifoplc));
	calls = 0;
}

void SDL_Delay(Uint32 ms)
{
	delay(ms * 1000u);
}

/* ---- video ---------------------------------------------------------------- */

static void clear_screen(void)
{
	memset(frame, 0, sizeof(frame));
	display_raw_write(0, 0, SCREEN_W, SCREEN_H / 2, frame, SCREEN_W * SCREEN_H);
	display_raw_write(0, SCREEN_H / 2, SCREEN_W, SCREEN_H / 2, frame,
			  SCREEN_W * SCREEN_H);
}

void spi_lcd_clear(void)
{
	clear_screen();
}

int SDL_Init(Uint32 flags)
{
	(void)flags;
	screen_fmt.BitsPerPixel = 8;
	screen_fmt.BytesPerPixel = 1;
	screen.format = &screen_fmt;
	screen.w = GAME_W;
	screen.h = GAME_H;
	screen.pitch = GAME_W;
	screen.pixels = pixels;
	screen.clip_rect.w = GAME_W;
	screen.clip_rect.h = GAME_H;
	screen.refcount = 1;
	return 0;
}

int SDL_InitSubSystem(Uint32 flags)
{
	return SDL_Init(flags);
}

void SDL_QuitSubSystem(Uint32 flags)
{
	(void)flags;
}

Uint32 SDL_WasInit(Uint32 flags)
{
	(void)flags;
	return 0;
}

void SDL_Quit(void)
{
}

void SDL_InitSD(void)
{
}

char *SDL_GetError(void)
{
	return "";
}

void SDL_ClearError(void)
{
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
	(void)width; (void)height; (void)bpp; (void)flags;
	return &screen;
}

SDL_Surface *SDL_GetVideoSurface(void)
{
	return &screen;
}

int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{
	(void)flags;
	return width == GAME_W && height == GAME_H && bpp == 8;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{
	static SDL_Rect mode = { 0, 0, GAME_W, GAME_H };
	static SDL_Rect *modes[] = { &mode, NULL };

	(void)format; (void)flags;
	return modes;
}

SDL_VideoInfo *SDL_GetVideoInfo(void)
{
	static SDL_VideoInfo info;

	return &info;
}

char *SDL_VideoDriverName(char *namebuf, int maxlen)
{
	(void)namebuf; (void)maxlen;
	return "akira";
}

void SDL_WM_SetCaption(const char *title, const char *icon)
{
	(void)title; (void)icon;
}

int SDL_LockSurface(SDL_Surface *surface)
{
	(void)surface;
	return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
	(void)surface;
}

void SDL_LockDisplay(void)
{
}

void SDL_UnlockDisplay(void)
{
}

Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b)
{
	(void)fmt; (void)r; (void)g; (void)b;
	return 0;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *r, Uint32 color)
{
	if (!dst) {
		return 0;
	}
	if (!r) {
		memset(dst->pixels, (uint8_t)color, dst->pitch * dst->h);
		return 0;
	}
	for (int y = r->y; y < r->y + r->h; y++) {
		memset((uint8_t *)dst->pixels + y * dst->pitch + r->x, (uint8_t)color, r->w);
	}
	return 0;
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int first, int n)
{
	(void)surface;
	for (int i = first; i < first + n; i++) {
		/* No byte swap: the AkiraConsole Prod panel reads the uint16 as is. */
		pal565[i] = ((colors[i].r & 0xF8) << 8) | ((colors[i].g & 0xFC) << 3) |
			    (colors[i].b >> 3);
	}
	return 1;
}

/* fps log: every FPS_LOG_MS, frames per second and the average ms per frame
 * spent waiting for the previous SPI transfer and converting the palette. */
#define FPS_LOG_MS 5000
static uint32_t fps_start, fps_frames, fps_wait_ms, fps_conv_ms;

static void fps_log(uint32_t now)
{
	if (!fps_start) {
		fps_start = now;
		return;
	}
	fps_frames++;
	if (now - fps_start >= FPS_LOG_MS) {
		uint32_t dt = now - fps_start;

		printf("duke3d: %u.%u fps, display wait %u.%u ms, convert %u.%u ms\n",
		       fps_frames * 1000 / dt, fps_frames * 10000 / dt % 10,
		       fps_wait_ms / fps_frames, fps_wait_ms * 10 / fps_frames % 10,
		       fps_conv_ms / fps_frames, fps_conv_ms * 10 / fps_frames % 10);
		fps_start = now;
		fps_frames = fps_wait_ms = fps_conv_ms = 0;
	}
}

int SDL_Flip(SDL_Surface *s)
{
	uint32_t t0 = SDL_GetTicks(), t1, t2;

	(void)s;
	/* The previous transfer reads this buffer until it completes. */
	if (frame_in_flight) {
		display_raw_wait();
	}
	t1 = SDL_GetTicks();

	/* 4 pixels per step: one 32-bit load, two 32-bit stores. */
	const uint32_t *src = (const uint32_t *)pixels;
	uint32_t *dst = (uint32_t *)frame;

	for (int i = 0; i < GAME_W * GAME_H / 4; i++) {
		uint32_t p = src[i];

		dst[2 * i] = pal565[p & 0xFF] | ((uint32_t)pal565[(p >> 8) & 0xFF] << 16);
		dst[2 * i + 1] = pal565[(p >> 16) & 0xFF] | ((uint32_t)pal565[p >> 24] << 16);
	}
	t2 = SDL_GetTicks();
	frame_in_flight = display_raw_write_async(0, GAME_Y, GAME_W, GAME_H, frame,
						  sizeof(frame)) == 0;

	last_flip_ms = t2;
	fps_wait_ms += t1 - t0;
	fps_conv_ms += t2 - t1;
	fps_log(t2);
	return 0;
}

void SDL_UpdateRect(SDL_Surface *s, Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
	(void)x; (void)y; (void)w; (void)h;
	SDL_Flip(s);
}

/* ---- no mouse ------------------------------------------------------------- */

const SDL_version *SDL_Linked_Version(void)
{
	static const SDL_version v = { SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL };

	return &v;
}

SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode)
{
	return mode;
}

int SDL_ShowCursor(int toggle)
{
	(void)toggle;
	return 0;
}

void SDL_SetModState(SDL_Keymod modstate)
{
	(void)modstate;
}

/* ---- input ---------------------------------------------------------------- */

#define EVQ_LEN 32
static struct { uint8_t down; SDL_Keycode key; } evq[EVQ_LEN];
static unsigned evq_head, evq_tail;

static void key_event(int down, SDL_Keycode key)
{
	if (evq_head - evq_tail < EVQ_LEN) {
		unsigned i = evq_head++ % EVQ_LEN;

		evq[i].down = (uint8_t)down;
		evq[i].key = key;
	}
}

/* A tap stays down for TAP_MS: Build drains all queued events each frame
 * and only looks at key state, so a press and release in the same batch
 * would never be seen. */
#define TAP_MS   100
#define TAPS_MAX 4
static struct { SDL_Keycode key; uint32_t up_ms; } taps[TAPS_MAX];

static void key_tap(SDL_Keycode key)
{
	for (int i = 0; i < TAPS_MAX; i++) {
		if (!taps[i].key) {
			taps[i].key = key;
			taps[i].up_ms = SDL_GetTicks() + TAP_MS;
			key_event(1, key);
			return;
		}
	}
}

static void release_taps(void)
{
	uint32_t now = SDL_GetTicks();

	for (int i = 0; i < TAPS_MAX; i++) {
		if (taps[i].key && (int32_t)(now - taps[i].up_ms) >= 0) {
			key_event(0, taps[i].key);
			taps[i].key = 0;
		}
	}
}

/* Enter is sent as keypad Enter: the vendored SDL.h aliases SDLK_KP0 to
 * SDLK_RETURN, so display.c maps Return to keypad 0 (Look_Left). Menus and
 * the Inventory binding accept keypad Enter. */
#define KEY_ENTER SDLK_KP_ENTER

/* Held keys, one per logical control, so a release always goes to the key
 * that was pressed even if Y or the menu state changed in between. */
enum { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_A, K_B, K_X, K_COUNT };
static SDL_Keycode held_key[K_COUNT];

static void set_held(int k, SDL_Keycode key)
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

static void poll_buttons(void)
{
	uint32_t b = (uint32_t)input_get_buttons();
	uint32_t pressed = b & ~prev_btns;

	release_taps();
	int y = (b & AKIRA_BTN_Y) != 0;
	int menu = keyMode != 0;

	set_held(K_UP,    (b & AKIRA_BTN_UP) && !(y && !menu) ? SDLK_UP : 0);
	set_held(K_DOWN,  (b & AKIRA_BTN_DOWN) && !(y && !menu) ? SDLK_DOWN : 0);
	set_held(K_LEFT,  (b & AKIRA_BTN_LEFT) ?
			  (y && !menu ? SDLK_COMMA : SDLK_LEFT) : 0);
	set_held(K_RIGHT, (b & AKIRA_BTN_RIGHT) ?
			  (y && !menu ? SDLK_PERIOD : SDLK_RIGHT) : 0);

	if (menu) {
		set_held(K_A, 0);
		set_held(K_B, 0);
		set_held(K_X, 0);
		if (pressed & AKIRA_BTN_A) {
			key_tap(KEY_ENTER);
		}
		if (pressed & AKIRA_BTN_B) {
			key_tap(SDLK_ESCAPE);
		}
		if (pressed & AKIRA_BTN_X) {
			key_tap(SDLK_y);
		}
	} else {
		set_held(K_A, (b & AKIRA_BTN_A) && !y ? SDLK_LCTRL : 0);
		set_held(K_B, (b & AKIRA_BTN_B) && !y ? SDLK_SPACE : 0);
		set_held(K_X, (b & AKIRA_BTN_X) ? (y ? SDLK_z : SDLK_a) : 0);
		if (y && (pressed & AKIRA_BTN_A)) {
			key_tap(SDLK_QUOTE);        /* next weapon */
		}
		if (y && (pressed & AKIRA_BTN_B)) {
			key_tap(KEY_ENTER);       /* use inventory item */
		}
		if (y && (pressed & AKIRA_BTN_UP)) {
			key_tap(SDLK_TAB);          /* map */
		}
		if (y && (pressed & AKIRA_BTN_DOWN)) {
			key_tap(SDLK_RIGHTBRACKET); /* next inventory item */
		}
	}

	uint32_t now = SDL_GetTicks();

	if (pressed & BTN_HOME) {
		home_down_ms = now;
	} else if ((prev_btns & BTN_HOME) && !(b & BTN_HOME) &&
		   (uint32_t)(now - home_down_ms) < HOME_LONG_MS) {
		key_tap(SDLK_ESCAPE);
	}

	prev_btns = b;
}

int SDL_PollEvent(SDL_Event *event)
{
	if (evq_head == evq_tail) {
		poll_buttons();
	}
	if (evq_head == evq_tail) {
		return 0;
	}

	unsigned i = evq_tail++ % EVQ_LEN;

	memset(event, 0, sizeof(*event));
	event->type = evq[i].down ? SDL_KEYDOWN : SDL_KEYUP;
	event->key.type = event->type;
	event->key.state = evq[i].down ? SDL_PRESSED : SDL_RELEASED;
	event->key.keysym.sym = evq[i].key;
	return 1;
}

/* ---- entry ---------------------------------------------------------------- */

int duke_main(int argc, char **argv);

/* The GRP and config live in the app's sandbox: /SD:/apps/duke3d/. */
int main(void)
{
	static char *argv[] = { "duke3d", "/nm", "/ns", NULL };

#ifdef DUKE_DIAG
	/* For the shell: akira peek duke3d <ring> 64, akira peek duke3d <pos> 1 */
	printf("duke3d diag: ring %u pos %u\n", (unsigned)(uintptr_t)diag_ring,
	       (unsigned)(uintptr_t)&diag_pos);
#endif

	return duke_main(3, argv);
}

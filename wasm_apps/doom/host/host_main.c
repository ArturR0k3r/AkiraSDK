/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Headless host backend, for measuring memory and checking the engine runs
 * with a given zone size. Not part of the AkiraOS app.
 *
 *   ./doom_host -iwad freedoom1.wad -mb 4 -timedemo demo1
 *   DOOM_FRAMES=700 ./doom_host -iwad freedoom1.wad -mb 4 -warp 1 8
 *
 * DOOM_FRAMES ends the run after that many frames (exit code 0).
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "doomgeneric.h"

static long frames, max_frames;

void DG_Init(void)
{
	const char *s = getenv("DOOM_FRAMES");

	max_frames = s ? atol(s) : 0;
}

void DG_DrawFrame(void)
{
	if (max_frames && ++frames >= max_frames) {
		struct mallinfo2 mi = mallinfo2();

		printf("host: %ld frames done, heap in use %zu bytes\n", frames,
		       mi.uordblks + mi.hblkhd);
		exit(0);
	}
}

void DG_SleepMs(uint32_t ms)
{
	usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
	(void)pressed;
	(void)key;
	return 0;
}

void DG_SetWindowTitle(const char *title)
{
	(void)title;
}

int main(int argc, char **argv)
{
	doomgeneric_Create(argc, argv);
	for (;;) {
		doomgeneric_Tick();
	}
}

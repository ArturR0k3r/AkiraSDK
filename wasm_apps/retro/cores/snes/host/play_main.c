/* Scripted-input host driver: play_host <rom> <script> <dump_frames_csv> <out_prefix>
 * script lines: "<frame> <hexpad>"  (pad holds from that frame until the next line). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../snes.h"

static SNES snes;

static void dump(const char *prefix, int frame)
{
    char name[256];
    snprintf(name, sizeof name, "%s_%05d.ppm", prefix, frame);
    FILE *f = fopen(name, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SNES_W, SNES_H);
    for (int i = 0; i < SNES_W * SNES_H; i++) {
        uint16_t c = snes.fb[i];
        fputc((c >> 11) << 3, f); fputc(((c >> 5) & 63) << 2, f); fputc((c & 31) << 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 5) return 1;
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 1;
    fclose(f);
    if (snes_init(&snes, rom, sz)) return 1;

    int sf[512], sp[512], n = 0;
    FILE *sc = fopen(argv[2], "r");
    unsigned fr, pad;
    while (sc && fscanf(sc, "%u %x", &fr, &pad) == 2 && n < 512) { sf[n] = fr; sp[n] = pad; n++; }
    int dumps[64], nd = 0, last = 0;
    for (char *t = strtok(argv[3], ","); t && nd < 64; t = strtok(NULL, ",")) { dumps[nd] = atoi(t); if (dumps[nd] > last) last = dumps[nd]; nd++; }

    int cur = 0;
    for (int i = 0; i <= last; i++) {
        int k;
        for (k = 0; k < n; k++) if (sf[k] <= i) cur = sp[k];
        snes_set_pad(&snes, 0, (uint16_t)cur);
        snes_run_frame(&snes);
        for (k = 0; k < nd; k++) if (dumps[k] == i) dump(argv[4], i);
    }
    return 0;
}

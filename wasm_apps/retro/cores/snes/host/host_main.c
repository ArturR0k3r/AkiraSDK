/* Host test driver: snes_host <rom> <frames> <out.ppm> [buttons-hex] */
#include <stdio.h>
#include <stdlib.h>
#include "../snes.h"

static SNES snes;

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s rom frames out.ppm [pad]\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("rom"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 1;
    fclose(f);
    if (snes_init(&snes, rom, sz)) { fprintf(stderr, "bad rom\n"); return 1; }
    fprintf(stderr, "hirom=%d sram=%u reset=%04X\n", snes.hirom, snes.sram_mask + (snes.sram_mask != 0), snes.cpu.pc);
    int frames = atoi(argv[2]);
    uint16_t pad = argc > 4 ? (uint16_t)strtol(argv[4], 0, 16) : 0;
    for (int i = 0; i < frames; i++) {
        snes_set_pad(&snes, 0, (i % 60) < 30 ? pad : 0);
        snes_run_frame(&snes);
    }
    f = fopen(argv[3], "wb");
    fprintf(f, "P6\n%d %d\n255\n", SNES_W, SNES_H);
    for (int i = 0; i < SNES_W * SNES_H; i++) {
        uint16_t c = snes.fb[i];
        fputc((c >> 11) << 3, f); fputc(((c >> 5) & 63) << 2, f); fputc((c & 31) << 3, f);
    }
    fclose(f);
    fprintf(stderr, "pc=%02X:%04X a=%04X nmi_en=%d inidisp=%02X tm=%02X bgmode=%d frame=%u\n",
            snes.cpu.pb, snes.cpu.pc, snes.cpu.a, snes.nmitimen >> 7, snes.ppu.inidisp, snes.ppu.tm,
            snes.ppu.bgmode & 7, snes.frame);
    return 0;
}

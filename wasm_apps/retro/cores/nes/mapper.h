/**
 * @file mapper.h
 * @brief NES ROM mapper abstraction
 *
 * Supported mappers:
 *   0  (NROM)  — Donkey Kong, Galaga, Super Mario Bros, Balloon Fight
 *   1  (MMC1)  — Tetris (U), The Legend of Zelda, Mega Man 2
 *   2  (UxROM) — Contra, Mega Man, Castlevania, Duck Tales
 *   3  (CNROM) — Arkanoid, Gradius, Paperboy
 *   4  (MMC3)  — Super Mario Bros 3, Kirby's Adventure, Mega Man 3–6
 *   7  (AxROM) — Battletoads, Wizards & Warriors, Marble Madness
 *   9  (MMC2)  — Punch-Out!!
 *  66  (GxROM) — Super Mario Bros. + Duck Hunt multicart, Gumshoe
 * 206 (Namco)  — Simplified MMC3 subset (Namco 108, Galaga '90)
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>

/* ── Mirroring modes ────────────────────────────────────────────────── */
#define MIRROR_H   0  /**< Horizontal:          NT0=NT1, NT2=NT3        */
#define MIRROR_V   1  /**< Vertical:            NT0=NT2, NT1=NT3        */
#define MIRROR_1A  2  /**< Single screen lower  (all NTs → VRAM[0])    */
#define MIRROR_1B  3  /**< Single screen upper  (all NTs → VRAM[1])    */

typedef struct {
    int           id;             /**< iNES mapper number                  */
    int           prg_banks;      /**< Number of 16 KB PRG-ROM banks       */
    int           chr_banks;      /**< Number of 8 KB CHR-ROM banks (0=RAM)*/
    int           mirroring;      /**< MIRROR_H / MIRROR_V / 1A / 1B      */
    const uint8_t *prg_rom;       /**< Pointer into bundled ROM data       */
    const uint8_t *chr_rom;       /**< Pointer into bundled ROM data       */
    uint8_t        chr_ram[8192]; /**< 8 KB CHR-RAM (used when chr_banks==0)*/
    int            irq_pending;   /**< Mapper is asserting the /IRQ line   */

    /** Pre-computed PRG-ROM page pointers (4 × 8 KB windows).
     *  Indexed as: prg_page[(addr >> 13) & 3][addr & 0x1FFF]
     *  Updated by mapper writes; avoids per-read dispatch.             */
    const uint8_t *prg_page[4];

    /** Pre-computed CHR page pointers (8 × 1 KB windows).
     *  Indexed as: chr_page[addr >> 10][addr & 0x3FF]
     *  Updated by mapper writes; avoids per-read dispatch.
     *  For Mapper 9 (MMC2 latch-based) this is NOT used; the latch
     *  side-effect requires going through mapper_ppu_read().           */
    const uint8_t *chr_page[8];

    /** Mapper-private state (zero-initialised on mapper_init) */
    union {
        /** Mapper 1 — MMC1 / SxROM */
        struct {
            uint8_t shift; /**< 5-bit serial shift register                */
            uint8_t count; /**< Bits written so far; resets after 5th     */
            uint8_t ctrl;  /**< Control ($8000–$9FFF target register)     */
            uint8_t chr0;  /**< CHR bank 0 ($A000–$BFFF target)          */
            uint8_t chr1;  /**< CHR bank 1 ($C000–$DFFF target)          */
            uint8_t prg;   /**< PRG bank ($E000–$FFFF target)            */
        } mmc1;

        /** Mapper 2 — UxROM */
        struct {
            uint8_t prg_bank; /**< Selected lower 16 KB PRG bank          */
        } uxrom;

        /** Mapper 3 — CNROM */
        struct {
            uint8_t chr_bank; /**< Selected 8 KB CHR bank (bits 1:0)      */
        } cnrom;

        /** Mapper 4 — MMC3 (also used by Mapper 206) */
        struct {
            uint8_t bank_sel;   /**< Bank select reg written at $8000 even */
            uint8_t regs[8];    /**< R0–R7 bank registers                  */
            uint8_t irq_latch;  /**< IRQ reload value                      */
            uint8_t irq_cnt;    /**< IRQ scanline counter                  */
            uint8_t irq_en;     /**< IRQ enabled flag                      */
            uint8_t irq_reload; /**< Reload counter on next scanline clock  */
        } mmc3;

        /** Mapper 7 — AxROM */
        struct {
            uint8_t prg_bank; /**< Selected 32 KB PRG bank (bits 2:0)     */
        } axrom;

        /** Mapper 9 — MMC2 / PxROM (Punch-Out!!) */
        struct {
            uint8_t prg_bank;       /**< 8 KB PRG bank at $8000           */
            uint8_t chr_bank_fd[2]; /**< CHR 4 KB bank when latch = $FD   */
            uint8_t chr_bank_fe[2]; /**< CHR 4 KB bank when latch = $FE   */
            uint8_t latch[2];       /**< 0=$FD active, 1=$FE active       */
        } mmc2;

        /** Mapper 66 — GxROM */
        struct {
            uint8_t prg_bank; /**< Selected 32 KB PRG bank (bits 5:4)     */
            uint8_t chr_bank; /**< Selected 8 KB CHR bank  (bits 1:0)     */
        } gxrom;
    } s;
} Mapper;

/**
 * Initialise mapper from iNES ROM image.
 * @param m        Mapper to initialise.
 * @param rom      Full iNES ROM (must stay valid for the lifetime of m).
 * @param rom_size Total size of the ROM image in bytes.
 * @return 0 on success, -1 if the mapper number is unsupported.
 */
int     mapper_init       (Mapper *m, const uint8_t *rom, int rom_size);

/** CPU-side read from cartridge space ($4020–$FFFF). */
uint8_t mapper_cpu_read   (const Mapper *m, uint16_t addr);

/** CPU-side write to cartridge space (mapper registers). */
void    mapper_cpu_write  (Mapper *m, uint16_t addr, uint8_t val);

/**
 * PPU-side read from pattern table space ($0000–$1FFF).
 * Non-const: Mapper 9 (MMC2) updates its CHR latches on certain reads.
 */
uint8_t mapper_ppu_read   (Mapper *m, uint16_t addr);

/** PPU-side write to $0000–$1FFF (CHR-RAM games only). */
void    mapper_ppu_write  (Mapper *m, uint16_t addr, uint8_t val);

/**
 * Notify mapper that a scanline has just completed rendering.
 * Must be called once per visible scanline (0–239) when rendering is active.
 * Drives the MMC3 IRQ counter.
 */
void    mapper_scanline   (Mapper *m);

/**
 * Check and clear the mapper IRQ pending flag.
 * @return 1 if the mapper was asserting /IRQ (flag cleared), 0 otherwise.
 */
int     mapper_irq_pending(Mapper *m);

/**
 * Recompute prg_page/chr_page pointer caches from current bank-select
 * state. Call after restoring a save state (see save_state.h).
 */
void    mapper_resync    (Mapper *m);

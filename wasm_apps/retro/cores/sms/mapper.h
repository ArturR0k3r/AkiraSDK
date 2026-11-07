/**
 * @file mapper.h
 * @brief SMS Sega mapper — ROM bank switching
 *
 * The Sega mapper is the only mapper used on the vast majority of SMS
 * cartridges.  It splits the 16KB ROM slots as follows:
 *
 *   Slot 0  $0000–$3FFF  Bank register at $FFFD
 *   Slot 1  $4000–$7FFF  Bank register at $FFFE
 *   Slot 2  $8000–$BFFF  Bank register at $FFFF
 *
 * Special case: the first 1KB ($0000–$03FF) of slot 0 always maps to the
 * first 1KB of ROM page 0, regardless of the bank register.  This ensures
 * the Z80 reset and interrupt vectors are always reachable.
 *
 * Each bank register holds an 8-bit page number.  Page size = 16KB.
 * A 512KB ROM has 32 pages (page numbers 0–31).
 *
 * Register $FFFC (RAM select):
 *   bit 3 — enable cartridge RAM for slot 2 (not supported here; rare)
 *   bit 2 — cartridge RAM page select
 *   bits 1-0 — unused
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>

#define MAPPER_PAGE_SIZE  0x4000u   /**< 16KB per bank/page      */
#define MAPPER_MAX_PAGES  64        /**< Up to 1MB ROM (64×16KB) */

typedef struct {
    const uint8_t *rom;            /**< Pointer to full ROM data            */
    uint32_t       rom_size;       /**< ROM size in bytes                   */
    uint32_t       num_pages;      /**< Total 16KB pages in ROM             */

    uint8_t        bank[3];        /**< Current page mapped into each slot  */
    uint8_t        ram_select;     /**< $FFFC register value                */

    /* Page base pointers — recomputed on every bank register write.
     * pages[0/1/2] point directly into rom[] for fast reads. */
    const uint8_t *pages[3];
} Mapper;

/**
 * Initialise the mapper with ROM data.
 * @param m        Mapper to initialise.
 * @param rom      ROM data (must stay valid).
 * @param rom_size ROM size in bytes.
 * @return 0 on success, -1 if too small.
 */
int mapper_init(Mapper *m, const uint8_t *rom, uint32_t rom_size);

/**
 * Read a byte from the banked ROM space ($0000–$BFFF).
 * Addresses outside that range should not be passed here.
 */
uint8_t mapper_read(const Mapper *m, uint16_t addr);

/**
 * Handle a mapper register write ($FFFC–$FFFF in the RAM mirror).
 */
void mapper_write(Mapper *m, uint16_t addr, uint8_t val);

/**
 * Recompute the pages[] pointer cache from bank[]/rom. Call after restoring
 * a save state (see save_state.h): bank registers are restored, but the
 * derived pointer cache is not part of the save format.
 */
void mapper_resync(Mapper *m);

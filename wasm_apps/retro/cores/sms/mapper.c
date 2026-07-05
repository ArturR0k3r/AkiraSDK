/**
 * @file mapper.c
 * @brief SMS Sega mapper implementation
 *
 * @license Apache-2.0
 */
#include "mapper.h"
#include <string.h>

/* Update the page base pointer for a slot after a bank register change.
 * We modulo the page number so games with short ROMs loop correctly. */
static void update_page(Mapper *m, int slot)
{
    uint32_t page = m->bank[slot] % m->num_pages;
    m->pages[slot] = m->rom + page * MAPPER_PAGE_SIZE;
}

int mapper_init(Mapper *m, const uint8_t *rom, uint32_t rom_size)
{
    if (rom_size < MAPPER_PAGE_SIZE) return -1;

    memset(m, 0, sizeof(Mapper));
    m->rom       = rom;
    m->rom_size  = rom_size;
    m->num_pages = rom_size / MAPPER_PAGE_SIZE;
    if (m->num_pages == 0) m->num_pages = 1;

    /* Power-on: banks 0, 1, 2 mapped to pages 0, 1, 2 */
    m->bank[0] = 0;
    m->bank[1] = 1;
    m->bank[2] = 2;
    update_page(m, 0);
    update_page(m, 1);
    update_page(m, 2);

    return 0;
}

void mapper_resync(Mapper *m)
{
    update_page(m, 0);
    update_page(m, 1);
    update_page(m, 2);
}

uint8_t mapper_read(const Mapper *m, uint16_t addr)
{
    int slot = addr >> 14;   /* 0=slot0, 1=slot1, 2=slot2 */

    /* First 1KB of slot 0 always maps to first 1KB of ROM page 0 */
    if (addr < 0x0400u)
        return m->rom[addr];

    return m->pages[slot][addr & (MAPPER_PAGE_SIZE - 1)];
}

void mapper_write(Mapper *m, uint16_t addr, uint8_t val)
{
    switch (addr) {
    case 0xFFFC:
        m->ram_select = val;
        break;
    case 0xFFFD:
        m->bank[0] = val;
        update_page(m, 0);
        break;
    case 0xFFFE:
        m->bank[1] = val;
        update_page(m, 1);
        break;
    case 0xFFFF:
        m->bank[2] = val;
        update_page(m, 2);
        break;
    }
}

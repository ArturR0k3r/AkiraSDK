/**
 * @file gb.c
 * @brief Game Boy / GBC machine — memory bus, MBC, I/O, timer, DMA, frame step
 *
 * Memory map:
 *   0x0000–0x3FFF  ROM bank 0
 *   0x4000–0x7FFF  ROM bank N (MBC-switched)
 *   0x8000–0x9FFF  VRAM (bank 0/1 on CGB)
 *   0xA000–0xBFFF  External (cartridge) RAM
 *   0xC000–0xCFFF  WRAM bank 0
 *   0xD000–0xDFFF  WRAM bank 1–7 (CGB) / bank 1 (DMG)
 *   0xE000–0xFDFF  Echo RAM (mirrors C000–DDFF)
 *   0xFE00–0xFE9F  OAM
 *   0xFEA0–0xFEFF  Not usable (reads 0xFF)
 *   0xFF00–0xFF7F  I/O registers
 *   0xFF80–0xFFFE  HRAM
 *   0xFFFF         Interrupt Enable
 *
 * @license Apache-2.0
 */
#include "gb.h"
#include <string.h>

/* ── Cartridge header offsets ────────────────────────────────────────── */
#define HDR_TITLE       0x0134
#define HDR_CGB_FLAG    0x0143
#define HDR_CART_TYPE   0x0147
#define HDR_ROM_SIZE    0x0148
#define HDR_RAM_SIZE    0x0149

/* ROM bank sizes for HDR_ROM_SIZE field values 0x00–0x08 */
static const uint32_t rom_bank_count[9] = {
    2, 4, 8, 16, 32, 64, 128, 256, 512
};

/* Cartridge RAM sizes for HDR_RAM_SIZE field */
static const uint32_t cart_ram_sizes[6] = {
    0, 2048, 8192, 32768, 131072, 65536
};

/* ── MBC type from cartridge type byte ───────────────────────────────── */
static uint8_t detect_mbc(uint8_t cart_type)
{
    switch (cart_type) {
        case 0x00:                    return MBC_NONE;
        case 0x01: case 0x02: case 0x03: return MBC_1;
        case 0x05: case 0x06:         return MBC_2;
        case 0x0F: case 0x10:
        case 0x11: case 0x12: case 0x13: return MBC_3;
        case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E: return MBC_5;
        default:                      return MBC_NONE;
    }
}

/* ── gb_init ─────────────────────────────────────────────────────────── */
void gb_init(GB *gb, const uint8_t *rom, uint32_t rom_size)
{
    memset(gb, 0, sizeof(*gb));

    gb->rom      = rom;
    gb->rom_size = rom_size;

    /* ROM banks */
    uint8_t rb = (rom_size > HDR_ROM_SIZE) ? rom[HDR_ROM_SIZE] : 0;
    if (rb < 9) gb->rom_banks = rom_bank_count[rb];
    else        gb->rom_banks = 2;

    /* Cart RAM size */
    uint8_t rs = (rom_size > HDR_RAM_SIZE) ? rom[HDR_RAM_SIZE] : 0;
    if (rs < 6) gb->cart_ram_size = cart_ram_sizes[rs];
    if (gb->cart_ram_size > GB_CART_RAM_MAX) gb->cart_ram_size = GB_CART_RAM_MAX;

    /* MBC type */
    gb->mbc_type = (rom_size > HDR_CART_TYPE) ? detect_mbc(rom[HDR_CART_TYPE]) : MBC_NONE;
    /* MBC2 has internal 512×4-bit RAM */
    if (gb->mbc_type == MBC_2 && gb->cart_ram_size == 0) gb->cart_ram_size = 512;

    /* CGB mode */
    uint8_t cgb_flag = (rom_size > HDR_CGB_FLAG) ? rom[HDR_CGB_FLAG] : 0;
    gb->cgb_mode = (cgb_flag == 0x80 || cgb_flag == 0xC0) ? 1 : 0;

    /* Default MBC state */
    gb->rom_bank  = 1;
    gb->ram_bank  = 0;
    gb->wram_bank = 1;

    /* Initial CGB palettes (white) */
    for (int p = 0; p < 8; p++)
        for (int c = 0; c < 4; c++) {
            gb->ppu.bg_pal[p][c]  = 0x7FFFu;
            gb->ppu.obj_pal[p][c] = 0x7FFFu;
        }

    /* Power-up CPU state (post-boot ROM) */
    gb->cpu.a  = gb->cgb_mode ? 0x11 : 0x01;
    gb->cpu.f  = 0xB0;  /* Z, H, C set */
    gb->cpu.b  = 0x00;
    gb->cpu.c  = 0x13;
    gb->cpu.d  = 0x00;
    gb->cpu.e  = 0xD8;
    gb->cpu.h  = 0x01;
    gb->cpu.l  = 0x4D;
    gb->cpu.sp = 0xFFFE;
    gb->cpu.pc = 0x0100;

    /* I/O reset values (post-boot) */
    gb->io[IO_P1]   = 0xCF;
    gb->io[IO_TIMA] = 0x00;
    gb->io[IO_TMA]  = 0x00;
    gb->io[IO_TAC]  = 0xF8;
    gb->io[IO_IF]   = 0xE1;
    gb->io[IO_LCDC] = 0x91;
    gb->io[IO_STAT] = 0x85;
    gb->io[IO_SCY]  = 0x00;
    gb->io[IO_SCX]  = 0x00;
    gb->io[IO_LY]   = 0x00;
    gb->io[IO_LYC]  = 0x00;
    gb->io[IO_BGP]  = 0xFC;
    gb->io[IO_OBP0] = 0xFF;
    gb->io[IO_OBP1] = 0xFF;
    gb->io[IO_WY]   = 0x00;
    gb->io[IO_WX]   = 0x00;
    gb->ie          = 0x00;
    gb->div_counter = 0xABCC;  /* Approximate post-boot DIV value */
}

/* ── Memory bus — read ───────────────────────────────────────────────── */
uint8_t gb_read(GB *gb, uint16_t addr)
{
    if (addr < 0x4000u) {
        /* ROM bank 0 */
        if (addr < gb->rom_size) return gb->rom[addr];
        return 0xFF;
    }
    if (addr < 0x8000u) {
        /* Switchable ROM bank */
        uint32_t bank  = gb->rom_bank % gb->rom_banks;
        uint32_t raddr = bank * 0x4000u + (addr - 0x4000u);
        if (raddr < gb->rom_size) return gb->rom[raddr];
        return 0xFF;
    }
    if (addr < 0xA000u) {
        /* VRAM */
        if (gb->ppu.mode == 3) return 0xFF; /* CPU can't access VRAM in mode 3 */
        return gb->vram[gb->vram_bank * 8192u + (addr & 0x1FFFu)];
    }
    if (addr < 0xC000u) {
        /* Cartridge RAM */
        if (!gb->ram_enable || gb->cart_ram_size == 0) return 0xFF;
        uint32_t raddr = (uint32_t)gb->ram_bank * 0x2000u + (addr & 0x1FFFu);
        if (raddr < gb->cart_ram_size) return gb->cart_ram[raddr];
        return 0xFF;
    }
    if (addr < 0xD000u) {
        /* WRAM bank 0 */
        return gb->wram[addr & 0x0FFFu];
    }
    if (addr < 0xE000u) {
        /* WRAM bank 1–7 */
        return gb->wram[gb->wram_bank * 0x1000u + (addr & 0x0FFFu)];
    }
    if (addr < 0xFE00u) {
        /* Echo RAM — mirror of C000–DDFF */
        uint16_t m = (uint16_t)(addr - 0x2000u);
        if (m < 0xD000u) return gb->wram[m & 0x0FFFu];
        return gb->wram[gb->wram_bank * 0x1000u + (m & 0x0FFFu)];
    }
    if (addr < 0xFEA0u) {
        /* OAM */
        if (gb->ppu.mode >= 2) return 0xFF; /* OAM blocked in modes 2+3 */
        return gb->oam[addr & 0x9Fu];
    }
    if (addr < 0xFF00u) {
        /* Prohibited area */
        return 0xFF;
    }
    if (addr < 0xFF80u) {
        /* I/O registers */
        uint8_t reg = (uint8_t)(addr & 0x7Fu);
        switch (reg) {
            case IO_P1: {
                uint8_t p1 = gb->io[IO_P1];
                uint8_t result = 0xCFu;
                if (!(p1 & 0x20u)) {
                    /* Button keys: A, B, Select, Start */
                    if (gb->joy_state & GB_BTN_A)      result &= ~0x01u;
                    if (gb->joy_state & GB_BTN_B)      result &= ~0x02u;
                    if (gb->joy_state & GB_BTN_SELECT) result &= ~0x04u;
                    if (gb->joy_state & GB_BTN_START)  result &= ~0x08u;
                }
                if (!(p1 & 0x10u)) {
                    /* D-pad: Right, Left, Up, Down */
                    if (gb->joy_state & GB_BTN_RIGHT)  result &= ~0x01u;
                    if (gb->joy_state & GB_BTN_LEFT)   result &= ~0x02u;
                    if (gb->joy_state & GB_BTN_UP)     result &= ~0x04u;
                    if (gb->joy_state & GB_BTN_DOWN)   result &= ~0x08u;
                }
                return (uint8_t)((p1 & 0x30u) | (result & 0x0Fu) | 0xC0u);
            }
            case IO_DIV:  return (uint8_t)(gb->div_counter >> 8);
            case IO_IF:   return (uint8_t)(gb->io[IO_IF] | 0xE0u);
            case IO_STAT: return (uint8_t)(gb->io[IO_STAT] | 0x80u);
            case IO_KEY1: return (uint8_t)((gb->double_speed ? 0x80u : 0x00u) |
                                            (gb->speed_prep ? 0x01u : 0x00u));
            case IO_VBK:  return (uint8_t)(gb->vram_bank | 0xFEu);
            case IO_SVBK: return (uint8_t)(gb->wram_bank | 0xF8u);
            case IO_HDMA5:
                return (gb->hdma_active == 2) ?
                    (uint8_t)((gb->hdma_remain / 16u) - 1u) : 0xFFu;
            case IO_BCPS: return (uint8_t)(gb->ppu.bg_pal_idx |
                                            (gb->ppu.bg_pal_idx ? 0x80u : 0x00u));
            case IO_BCPD: {
                uint8_t idx = gb->ppu.bg_pal_idx & 0x3Fu;
                return gb->ppu.bg_pal_ram[idx];
            }
            case IO_OCPS: return (uint8_t)(gb->ppu.obj_pal_idx |
                                            (gb->ppu.obj_pal_idx ? 0x80u : 0x00u));
            case IO_OCPD: {
                uint8_t idx = gb->ppu.obj_pal_idx & 0x3Fu;
                return gb->ppu.obj_pal_ram[idx];
            }
            default: return gb->io[reg];
        }
    }
    if (addr < 0xFFFFu) {
        /* HRAM */
        return gb->hram[addr & 0x7Fu];
    }
    /* 0xFFFF: IE */
    return gb->ie;
}

/* ── CGB palette write helper ────────────────────────────────────────── */
static void cgb_pal_write(uint8_t *pal_ram, uint16_t (*pal_rgb)[4],
                           uint8_t *idx_reg, uint8_t idx_byte, uint8_t data)
{
    uint8_t idx = idx_byte & 0x3Fu;
    pal_ram[idx] = data;
    /* Recompute the colour for this palette/color slot */
    uint8_t slot  = (uint8_t)(idx >> 1);
    uint8_t pal   = (uint8_t)(slot >> 2);
    uint8_t col   = (uint8_t)(slot & 3u);
    uint8_t lo    = pal_ram[slot * 2];
    uint8_t hi    = pal_ram[slot * 2 + 1];
    pal_rgb[pal][col] = cgb_to_rgb565(lo, hi);
    if (idx_byte & 0x80u) {
        *idx_reg = (uint8_t)((idx_byte & 0x80u) | ((idx + 1u) & 0x3Fu));
    }
}

/* ── Memory bus — write ──────────────────────────────────────────────── */
void gb_write(GB *gb, uint16_t addr, uint8_t val)
{
    if (addr < 0x8000u) {
        /* MBC register writes */
        switch (gb->mbc_type) {
        case MBC_NONE: break;
        case MBC_1:
            if (addr < 0x2000u) {
                gb->ram_enable = ((val & 0x0Fu) == 0x0Au);
            } else if (addr < 0x4000u) {
                uint8_t lower = val & 0x1Fu;
                if (lower == 0) lower = 1;
                gb->rom_bank = (uint16_t)((gb->mbc1_hi << 5) | lower);
            } else if (addr < 0x6000u) {
                gb->mbc1_hi = val & 0x03u;
                if (gb->mbc1_mode)
                    gb->ram_bank = gb->mbc1_hi;
                else
                    gb->rom_bank = (uint16_t)((gb->mbc1_hi << 5) |
                                              (gb->rom_bank & 0x1Fu));
            } else {
                gb->mbc1_mode = val & 0x01u;
                if (gb->mbc1_mode) {
                    gb->ram_bank = gb->mbc1_hi;
                    gb->rom_bank &= 0x1Fu;
                } else {
                    gb->ram_bank = 0;
                }
            }
            break;
        case MBC_2:
            if (addr < 0x4000u) {
                if (addr & 0x0100u) {
                    uint8_t bank = val & 0x0Fu;
                    if (bank == 0) bank = 1;
                    gb->rom_bank = bank;
                } else {
                    gb->ram_enable = ((val & 0x0Fu) == 0x0Au);
                }
            }
            break;
        case MBC_3:
            if (addr < 0x2000u) {
                gb->ram_enable = ((val & 0x0Fu) == 0x0Au);
            } else if (addr < 0x4000u) {
                uint8_t bank = val & 0x7Fu;
                if (bank == 0) bank = 1;
                gb->rom_bank = bank;
            } else if (addr < 0x6000u) {
                gb->ram_bank = val & 0x03u;
            }
            /* RTC registers (0x08–0x0C) not emulated */
            break;
        case MBC_5:
            if (addr < 0x2000u) {
                gb->ram_enable = ((val & 0x0Fu) == 0x0Au);
            } else if (addr < 0x3000u) {
                gb->rom_bank = (uint16_t)((gb->rom_bank & 0x100u) | val);
            } else if (addr < 0x4000u) {
                gb->rom_bank = (uint16_t)((gb->rom_bank & 0xFFu) | ((val & 1u) << 8));
            } else if (addr < 0x6000u) {
                gb->ram_bank = val & 0x0Fu;
            }
            break;
        }
        return;
    }

    if (addr < 0xA000u) {
        /* VRAM — blocked during mode 3 */
        if (gb->ppu.mode == 3) return;
        gb->vram[gb->vram_bank * 8192u + (addr & 0x1FFFu)] = val;
        return;
    }

    if (addr < 0xC000u) {
        /* Cartridge RAM */
        if (!gb->ram_enable || gb->cart_ram_size == 0) return;
        uint32_t raddr = (uint32_t)gb->ram_bank * 0x2000u + (addr & 0x1FFFu);
        /* MBC2: only lower nibble stored */
        if (gb->mbc_type == MBC_2) val |= 0xF0u;
        if (raddr < gb->cart_ram_size) gb->cart_ram[raddr] = val;
        return;
    }

    if (addr < 0xD000u) { gb->wram[addr & 0x0FFFu] = val; return; }
    if (addr < 0xE000u) { gb->wram[gb->wram_bank * 0x1000u + (addr & 0x0FFFu)] = val; return; }
    if (addr < 0xFE00u) {
        /* Echo RAM */
        uint16_t m = (uint16_t)(addr - 0x2000u);
        if (m < 0xD000u) gb->wram[m & 0x0FFFu] = val;
        else             gb->wram[gb->wram_bank * 0x1000u + (m & 0x0FFFu)] = val;
        return;
    }
    if (addr < 0xFEA0u) {
        if (gb->ppu.mode < 2) gb->oam[addr & 0x9Fu] = val;
        return;
    }
    if (addr < 0xFF00u) return; /* Prohibited */

    if (addr < 0xFF80u) {
        /* I/O registers */
        uint8_t reg = (uint8_t)(addr & 0x7Fu);
        switch (reg) {
            case IO_P1:
                gb->io[IO_P1] = (uint8_t)((val & 0x30u) | 0xCFu);
                break;
            case IO_SC:
                gb->io[IO_SC] = val;
                if (val & 0x80u) {
                    /* Transfer started. With internal clock (bit 0 = 1)
                     * it completes in 512 T-cycles.  With external clock
                     * (bit 0 = 0, no link cable) we simulate a disconnected
                     * slave: complete after 2048 T-cycles so the game
                     * doesn't hang waiting for a partner that never comes. */
                    gb->serial_timer = (val & 0x01u) ? 512 : 2048;
                }
                break;
            case IO_DIV:
                /* Writing any value resets DIV */
                gb->div_counter = 0;
                gb->io[IO_DIV]  = 0;
                break;
            case IO_TIMA: gb->io[IO_TIMA] = val; break;
            case IO_TMA:  gb->io[IO_TMA]  = val; break;
            case IO_TAC:  gb->io[IO_TAC]  = (uint8_t)(val | 0xF8u); break;
            case IO_IF:   gb->io[IO_IF]   = (uint8_t)(val | 0xE0u); break;
            case IO_STAT:
                gb->io[IO_STAT] = (uint8_t)((gb->io[IO_STAT] & 0x07u) | (val & 0x78u));
                break;
            case IO_LY:   /* Read-only */ break;
            case IO_LYC:  gb->io[IO_LYC] = val; break;
            case IO_DMA: {
                /* OAM DMA: source = val << 8, copy 160 bytes to OAM */
                gb->io[IO_DMA]  = val;
                gb->dma_active  = 1;
                gb->dma_src     = (uint16_t)((uint16_t)val << 8);
                gb->dma_pos     = 0;
                break;
            }
            case IO_KEY1:
                if (gb->cgb_mode) gb->speed_prep = val & 0x01u;
                break;
            case IO_VBK:
                if (gb->cgb_mode) {
                    gb->vram_bank = val & 0x01u;
                    gb->io[IO_VBK] = (uint8_t)(gb->vram_bank | 0xFEu);
                }
                break;
            case IO_HDMA1: gb->io[IO_HDMA1] = val; break;
            case IO_HDMA2: gb->io[IO_HDMA2] = val; break;
            case IO_HDMA3: gb->io[IO_HDMA3] = val; break;
            case IO_HDMA4: gb->io[IO_HDMA4] = val; break;
            case IO_HDMA5:
                if (gb->cgb_mode) {
                    uint16_t src = (uint16_t)(((uint16_t)gb->io[IO_HDMA1] << 8) |
                                               (gb->io[IO_HDMA2] & 0xF0u));
                    uint16_t dst = (uint16_t)(((uint16_t)(gb->io[IO_HDMA3] & 0x1Fu) << 8) |
                                               (gb->io[IO_HDMA4] & 0xF0u));
                    uint16_t len = (uint16_t)(((val & 0x7Fu) + 1u) * 16u);

                    if (gb->hdma_active == 2 && !(val & 0x80u)) {
                        /* Cancel HBlank DMA */
                        gb->hdma_active  = 0;
                        gb->io[IO_HDMA5] = 0xFFu;
                    } else if (val & 0x80u) {
                        /* HBlank DMA */
                        gb->hdma_active  = 2;
                        gb->hdma_src     = src;
                        gb->hdma_dst     = dst;
                        gb->hdma_remain  = len;
                        gb->io[IO_HDMA5] = (uint8_t)((len / 16u) - 1u);
                    } else {
                        /* General purpose DMA — execute immediately */
                        gb->hdma_active = 1;
                        for (uint16_t i = 0; i < len; i++) {
                            uint8_t v = gb_read(gb, (uint16_t)(src + i));
                            gb->vram[gb->vram_bank * 8192u +
                                     ((dst + i) & 0x1FFFu)] = v;
                        }
                        gb->hdma_active  = 0;
                        gb->io[IO_HDMA5] = 0xFFu;
                    }
                }
                break;
            case IO_BCPS:
                gb->ppu.bg_pal_idx = (uint8_t)(val & 0xBFu);
                gb->io[IO_BCPS]    = gb->ppu.bg_pal_idx;
                break;
            case IO_BCPD:
                cgb_pal_write(gb->ppu.bg_pal_ram, gb->ppu.bg_pal,
                              &gb->ppu.bg_pal_idx, gb->ppu.bg_pal_idx, val);
                break;
            case IO_OCPS:
                gb->ppu.obj_pal_idx = (uint8_t)(val & 0xBFu);
                gb->io[IO_OCPS]     = gb->ppu.obj_pal_idx;
                break;
            case IO_OCPD:
                cgb_pal_write(gb->ppu.obj_pal_ram, gb->ppu.obj_pal,
                              &gb->ppu.obj_pal_idx, gb->ppu.obj_pal_idx, val);
                break;
            case IO_SVBK:
                if (gb->cgb_mode) {
                    gb->wram_bank = (uint8_t)((val & 0x07u) ? (val & 0x07u) : 1u);
                    gb->io[IO_SVBK] = gb->wram_bank;
                }
                break;
            default:
                gb->io[reg] = val;
                break;
        }
        return;
    }
    if (addr < 0xFFFFu) {
        gb->hram[addr & 0x7Fu] = val;
        return;
    }
    gb->ie = val;
}

/* ── Timer tick ──────────────────────────────────────────────────────── */
/* Timer clock select → which bit of div_counter triggers increment */
static const uint16_t tac_bit[4] = { 512u, 8u, 32u, 128u };

static void timer_tick(GB *gb, int cycles)
{
    uint16_t old_div = gb->div_counter;
    gb->div_counter  = (uint16_t)(gb->div_counter + (uint16_t)cycles);
    gb->io[IO_DIV]   = (uint8_t)(gb->div_counter >> 8);

    /* TIMA reload after 1-cycle delay */
    if (gb->tima_reload_cnt > 0) {
        if ((int)gb->tima_reload_cnt <= cycles) {
            gb->io[IO_TIMA]   = gb->io[IO_TMA];
            gb->io[IO_IF]    |= INT_TIMER;
            gb->tima_reload_cnt = 0;
        } else {
            gb->tima_reload_cnt -= (uint8_t)cycles;
        }
    }

    uint8_t tac = gb->io[IO_TAC];
    if (!(tac & 0x04u)) return; /* Timer disabled */

    uint16_t bit = tac_bit[tac & 0x03u];

    /* Count falling edges of the selected bit */
    for (int i = 0; i < cycles; i++) {
        uint16_t prev = old_div + (uint16_t)i;
        uint16_t next = prev + 1u;
        if ((prev & bit) && !(next & bit)) {
            if (++gb->io[IO_TIMA] == 0) {
                /* Overflow: reload with TMA after 4 T-cycles */
                gb->tima_reload_cnt = 4;
            }
        }
    }
}

/* ── Serial tick ─────────────────────────────────────────────────────── */
static void serial_tick(GB *gb, int cycles)
{
    if (gb->serial_timer <= 0) return;
    gb->serial_timer -= cycles;
    if (gb->serial_timer <= 0) {
        gb->serial_timer = 0;
        gb->io[IO_SB] = 0xFF;         /* No slave connected → receive all 1s */
        gb->io[IO_SC] &= 0x7Fu;       /* Clear transfer-in-progress bit */
        gb->io[IO_IF] |= INT_SERIAL;
    }
}

/* ── OAM DMA tick ────────────────────────────────────────────────────── */
static void dma_tick(GB *gb, int cycles)
{
    if (!gb->dma_active) return;
    for (int i = 0; i < cycles && gb->dma_pos < GB_OAM_SIZE; i++) {
        /* Source can be rom, wram, or vram */
        gb->oam[gb->dma_pos] = gb_read(gb, (uint16_t)(gb->dma_src + gb->dma_pos));
        gb->dma_pos++;
    }
    if (gb->dma_pos >= GB_OAM_SIZE) {
        gb->dma_active = 0;
    }
}

/* ── gb_set_buttons ──────────────────────────────────────────────────── */
void gb_set_buttons(GB *gb, uint8_t btns)
{
    uint8_t prev = gb->joy_state;
    gb->joy_state = btns;
    /* Trigger joypad interrupt on any falling edge (button press) */
    if (!prev && btns) {
        gb->io[IO_IF] |= INT_JOYPAD;
    }
}

/* ── gb_step_frame ───────────────────────────────────────────────────── */
void gb_step_frame(GB *gb)
{
    gb->ppu.frame_ready = 0;

    while (!gb->ppu.frame_ready) {
        int cycles = sm83_step(gb);

        /* In double-speed mode the CPU runs at 2× but sub-systems stay at 1× */
        int sys_cycles = gb->double_speed ? (cycles >> 1) : cycles;

        timer_tick(gb, cycles);
        serial_tick(gb, cycles);
        ppu_tick(gb, sys_cycles);
        dma_tick(gb, sys_cycles);
    }
}

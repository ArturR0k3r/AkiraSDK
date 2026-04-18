/*
 * assets_palette.h — Color palette for Glasses Mystery pixel art
 * RGB565 format: 5-bit Red, 6-bit Green, 5-bit Blue
 * Short names for compact tile/sprite data notation.
 *
 * ST7789V via Zephyr MIPI DBI SPI sends raw bytes with no byte-swap.
 * Host is little-endian (ESP32-S3), SPI expects big-endian (MSB first).
 * We pre-swap all uint16_t color values so they appear correct on screen.
 */
#ifndef ASSETS_PALETTE_H
#define ASSETS_PALETTE_H

/* Byte-swap: host LE uint16_t → BE for ST7789V SPI */
#define SW(x) ((uint16_t)(((x) >> 8) | (((x) & 0xFF) << 8)))

/* Special */
#define BK 0x0000              /* black                        */
#define WH SW(0xFFFF)          /* white                        */
#define TR SW(0xF81F)          /* transparent (magenta key)    */

/* Greens — dark olive, low saturation for TFT */
#define G0 SW(0x0941)          /* very dark green  RGB(8,40,8)      */
#define G1 SW(0x1201)          /* dark green       RGB(16,64,8)     */
#define G2 SW(0x1AA2)          /* forest green     RGB(24,84,16)    */
#define G3 SW(0x2322)          /* main grass       RGB(32,100,16)   */
#define G4 SW(0x3403)          /* light green      RGB(48,128,24)   */
#define G5 SW(0x4D05)          /* bright green     RGB(72,160,40)   */

/* Browns — warm orange-brown */
#define B0 SW(0x4141)          /* dark brown       RGB(64,40,8)     */
#define B1 SW(0x7243)          /* medium brown     RGB(112,72,24)   */
#define B2 SW(0x9B45)          /* wood brown       RGB(152,104,40)  */
#define B3 SW(0xBC89)          /* light brown      RGB(184,144,72)  */
#define B4 SW(0xD5D0)          /* tan/beige        RGB(208,184,128) */

/* Grays — warm-tinted to counter TFT blue shift */
#define A0 SW(0x18C2)          /* very dark gray   RGB(24,24,16)    */
#define A1 SW(0x39C6)          /* dark gray        RGB(56,56,48)    */
#define A2 SW(0x6B4C)          /* medium gray      RGB(104,104,96)  */
#define A3 SW(0xA512)          /* light gray       RGB(160,160,144) */
#define A4 SW(0xCE57)          /* very light gray  RGB(200,200,184) */

/* Blues — deep navy */
#define L0 SW(0x0089)          /* dark blue        RGB(0,16,72)     */
#define L1 SW(0x094F)          /* water blue       RGB(8,40,120)    */
#define L2 SW(0x2294)          /* medium blue      RGB(32,80,160)   */
#define L3 SW(0x4C19)          /* sky blue         RGB(72,128,200)  */

/* Reds — warm brick */
#define E0 SW(0x78E1)          /* dark brick       RGB(120,28,8)    */
#define E1 SW(0xA162)          /* brick red        RGB(160,44,16)   */
#define E2 SW(0xCA03)          /* bright red       RGB(200,64,24)   */

/* Sand / Beige */
#define S0 SW(0xA44A)          /* dark sand        RGB(160,136,80)  */
#define S1 SW(0xC58F)          /* sand             RGB(192,176,120) */
#define S2 SW(0xDE94)          /* light sand       RGB(216,208,160) */

/* Purple — anomaly zones */
#define P0 SW(0x406B)          /* dark purple      RGB(64,12,88)    */
#define P1 SW(0x6913)          /* purple           RGB(104,32,152)  */
#define P2 SW(0x9A59)          /* light purple     RGB(152,72,200)  */

/* Teal — crystals, magic */
#define C0 SW(0x044D)          /* teal             RGB(0,136,104)   */
#define C1 SW(0x25D3)          /* bright teal      RGB(32,184,152)  */

/* Yellow / Gold */
#define D0 SW(0xCD00)          /* gold             RGB(200,160,0)   */
#define D1 SW(0xE6C3)          /* yellow           RGB(224,216,24)  */

/* Other */
#define O0 SW(0xCBC0)          /* orange           RGB(200,120,0)   */
#define SK SW(0xD50E)          /* skin tone        RGB(208,160,112) */
#define F0 SW(0xCB00)          /* fire/flame       RGB(200,96,0)    */
#define F1 SW(0xDDC4)          /* fire bright      RGB(216,184,32)  */

#endif /* ASSETS_PALETTE_H */

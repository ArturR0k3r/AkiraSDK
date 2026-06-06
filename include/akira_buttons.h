/*
 * akira_buttons.h — AkiraConsole hardware button GPIO assignments
 *
 * Hardware: all 9 buttons have external 10 kΩ pull-ups to +3.3 V and
 * switch to GND when pressed.  Every pin must be configured as
 * GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW so that gpio_read()
 * returns 1 when pressed and 0 when released.
 *
 *  GPIO  Signal
 *    0   BTN.DK  (OK / Settings — boot strap pin)
 *    4   BTN.U   (D-pad Up)
 *    5   BTN.D   (D-pad Down)
 *    6   BTN.L   (D-pad Left)
 *    7   BTN.R   (D-pad Right)
 *   15   BTN.A
 *   16   BTN.B
 *   17   BTN.X
 *   40   BTN.Y
 */
#ifndef AKIRA_BUTTONS_H
#define AKIRA_BUTTONS_H

#define BTN_OK       0   /* BTN.DK — SETTINGS / OK */
#define BTN_UP       4   /* BTN.U */
#define BTN_DOWN     5   /* BTN.D */
#define BTN_LEFT     6   /* BTN.L */
#define BTN_RIGHT    7   /* BTN.R */
#define BTN_A       15   /* BTN.A */
#define BTN_B       16   /* BTN.B */
#define BTN_X       17   /* BTN.X */
#define BTN_Y       40   /* BTN.Y */

/* Aliases used by older apps */
#define BTN_SETTINGS BTN_OK
#define PIN_UP       BTN_UP
#define PIN_DOWN     BTN_DOWN
#define PIN_LEFT     BTN_LEFT
#define PIN_RIGHT    BTN_RIGHT
#define PIN_A        BTN_A
#define PIN_B        BTN_B
#define PIN_X        BTN_X
#define PIN_Y        BTN_Y
#define PIN_SET      BTN_OK
#define PIN_SETTINGS BTN_OK
#define PIN_CENTER   BTN_A  /* AkiraVault alias */

/* Single config flag for every button (external pull-up, active-low) */
#define BTN_GPIO_FLAGS (GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW)

/* Configure all 9 buttons */
static inline void akira_buttons_init(void)
{
    gpio_configure(BTN_OK,    BTN_GPIO_FLAGS);
    gpio_configure(BTN_UP,    BTN_GPIO_FLAGS);
    gpio_configure(BTN_DOWN,  BTN_GPIO_FLAGS);
    gpio_configure(BTN_LEFT,  BTN_GPIO_FLAGS);
    gpio_configure(BTN_RIGHT, BTN_GPIO_FLAGS);
    gpio_configure(BTN_A,     BTN_GPIO_FLAGS);
    gpio_configure(BTN_B,     BTN_GPIO_FLAGS);
    gpio_configure(BTN_X,     BTN_GPIO_FLAGS);
    gpio_configure(BTN_Y,     BTN_GPIO_FLAGS);
}

#endif /* AKIRA_BUTTONS_H */

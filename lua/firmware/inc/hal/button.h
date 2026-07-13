/**
 * @file button.h
 * @brief Head push-button low-level access.
 */
#ifndef _BUTTON_H_
#define _BUTTON_H_

#include <stdint.h>

/** @brief Configure the button GPIO (P3.1) as input. Call once at startup. */
void init_button(void);

/** @brief Read the head button. @return 1 = pressed, 0 = released.
 *  Active-low GPIO on P3.1, polled (no IRQ). */
uint8_t button_pressed(void);

#endif

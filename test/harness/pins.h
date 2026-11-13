// Harness stand-in for the board's pins.h, which the real watch.h includes.
// It has the button reads faces and movement make; harness_hw.c returns the
// state harness_button_down/up set. Any other pin a face touches is
// undeclared, so that face doesn't compile until the harness supports it.
#pragma once

#include <stdbool.h>

bool HAL_GPIO_BTN_LIGHT_read(void);
bool HAL_GPIO_BTN_MODE_read(void);
bool HAL_GPIO_BTN_ALARM_read(void);

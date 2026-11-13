// Internal to the harness: the fake hardware in harness_hw.c, as harness.c
// drives it. Tests use harness.h.
#ifndef HARNESS_HW_H_
#define HARNESS_HW_H_

#include <stdbool.h>

#include "harness.h"

// Clears the RTC callbacks, buzzer, LED, button pins and sleep animation, and
// sets the RTC to `utc` at the start of a second.
void hw_reset(watch_date_time_t utc);

// Advances the RTC counter by one count (1/128 s) and fires what is due at
// the new count, in this order: the periodic (tick) callback, compare
// callbacks (one count after their match) by index, the end of a buzzer
// sequence. Returns true if any of them fired, that is, if the watch would
// have woken for an interrupt.
bool hw_step(void);

void hw_set_button(harness_button_t button, bool down);

// What watch_enter_sleep_mode does to the RTC: the periodic callbacks stop.
void hw_disable_periodic_callbacks(void);

#endif // HARNESS_HW_H_

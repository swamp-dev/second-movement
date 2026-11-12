/*
 * MIT License
 *
 * Copyright (c) 2026 second-movement contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Sunrise and sunset as Unix times, so a face can show them in any time zone
// without wrapping hours by hand. sunriset gives hours of a UT date; these
// functions turn them into instants and correct sunriset's antimeridian wrap.
// No hardware dependencies, so it also builds on a PC for the host tests in test/.

#ifndef SUN_EVENTS_H_
#define SUN_EVENTS_H_

#include <stdint.h>

typedef enum {
    SUN_EVENT_RISE,
    SUN_EVENT_SET,
    SUN_EVENT_NO_RISE,  // a polar night: the Sun stays below the horizon all day
    SUN_EVENT_NO_SET,   // a polar day: the Sun stays above the horizon all day
} sun_event_kind_t;

typedef struct {
    int64_t t;  // Unix time (UTC seconds). For NO_RISE and NO_SET, that day's local mean noon.
    sun_event_kind_t kind;
} sun_event_t;

// Sunrise and sunset of the day whose local mean noon falls on `day` (days since
// 1970-01-01) at the given latitude and longitude (degrees, north and east
// positive), as Unix times, using the upper limb 35 arc minutes below the
// horizon. Returns 0 on success, +1 when the Sun stays above the horizon all
// day and -1 when it stays below, as sunriset does; *rise and *set are then
// left unchanged.
int sun_events_of_day(int64_t day, double latitude, double longitude, int64_t *rise, int64_t *set);

#define SUN_EVENTS_MAX_AHEAD 3

// The upcoming sunrise or sunset strictly after `now`, skipping `skip` of them
// first (0 is the next one). A polar day or night counts as one event, at its
// local mean noon, in place of that day's sunrise and sunset. `skip` is at most
// SUN_EVENTS_MAX_AHEAD; a larger value is treated as that.
sun_event_t sun_events_upcoming(int64_t now, unsigned skip, double latitude, double longitude);

#endif // SUN_EVENTS_H_

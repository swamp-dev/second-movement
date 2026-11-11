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

// Planetary hours: the time from one sunrise to the next is split into 12
// equal day hours (sunrise to sunset) and 12 equal night hours (sunset to the
// next sunrise). The first hour after sunrise is ruled by the planet of the
// weekday, and each following hour by the next planet in the Chaldean order,
// without a break across days. No hardware dependencies, so it also builds on a
// PC for the host tests in test/.

#ifndef PLANETARY_H_
#define PLANETARY_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;          // 5 characters, for the custom LCD
    const char *abbreviation;  // 2 characters, for the classic LCD
} planetary_planet_t;

#define PLANETARY_PLANET_COUNT 7

// The Chaldean order: Saturn, Jupiter, Mars, Sun, Venus, Mercury, Moon.
extern const planetary_planet_t planetary_planets[PLANETARY_PLANET_COUNT];

typedef struct {
    uint8_t ruler;        // index into planetary_planets[]
    uint8_t hour_number;  // 0-23 counted from sunrise; 0-11 are day hours, 12-23 night hours
    int64_t start;        // Unix time (UTC seconds) the hour starts
    int64_t end;          // Unix time the hour ends, which is when the next one starts
} planetary_hour_t;

// The planetary hour containing the instant `now` (Unix time, UTC) at the given
// latitude and longitude (degrees, north and east positive). `utc_offset` is
// the local offset from UTC in seconds and only decides which calendar day,
// and so which weekday, a sunrise belongs to. With an offset far from the
// location's solar time (sunrise near local midnight), that local date can
// repeat or skip a day, and the ruler sequence jumps there.
// Returns false, leaving *out unchanged, when there is no sunrise or no sunset
// close enough to `now` (polar day or polar night).
bool planetary_hour_at(int64_t now, int32_t utc_offset, double latitude, double longitude,
                       planetary_hour_t *out);

// The planetary hour `steps` hours after *from (before it when negative),
// counted by hour number, so every step lands on the next or previous hour
// whatever their length. *from must come from planetary_hour_at() or this
// function for the same place; *out may be the same struct.
// Returns false, leaving *out unchanged, when a step reaches polar day or night.
bool planetary_hour_step(const planetary_hour_t *from, int steps, int32_t utc_offset,
                         double latitude, double longitude, planetary_hour_t *out);

// What a face browsing planetary hours shows: the hour `steps` hours from the
// one containing `now`. *changes_at is when that answer next changes, the end
// of the hour containing `now`, after which it is the following hour, for any
// `steps`. Returns false, leaving *out unchanged, on polar day or night. Even
// then *changes_at is set when only the browsed hour is polar, since the answer
// can still change when the current hour ends; it is left unchanged when the
// current hour itself is polar.
bool planetary_hour_from_now(int64_t now, int steps, int32_t utc_offset, double latitude,
                             double longitude, planetary_hour_t *out, int64_t *changes_at);

#endif // PLANETARY_H_

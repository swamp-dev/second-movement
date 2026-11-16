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

// Which segments draw the moon for a phase. No watch or movement calls.

#ifndef MOON_SHAPE_H_
#define MOON_SHAPE_H_

#include <stdbool.h>
#include <stdint.h>

// Masks for segment_art_draw (bit 0 is segment A, bit 7 is H).
typedef struct {
    // Classic LCD: position 0, the first top-left character.
    uint8_t classic;
    // Custom LCD: positions 0 and 1, the first two top-left characters, which
    // together show one disc. The lit part of the moon is drawn as an
    // outline: nothing at new moon, the right character's bracket for a
    // crescent, a closed half for a quarter, and the whole ring when full.
    // H, the upright in the middle of a character, closes the shape between
    // those steps.
    uint8_t custom_left;
    uint8_t custom_right;
} moon_shape_t;

// phase: 0 new, 1 waxing crescent, 2 first quarter, 3 waxing gibbous, 4 full,
// 5 waning gibbous, 6 third quarter, 7 waning crescent. Anything higher is
// drawn as new.
// fraction: how far through the lunar month, 0 to 1. It splits each crescent
// into a thin and a thick half, at an eighth of the month from the new moon.
// southern: the lit side swaps, as the moon is seen south of the equator.
moon_shape_t moon_shape(uint8_t phase, double fraction, bool southern);

#endif // MOON_SHAPE_H_

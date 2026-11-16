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

#include "moon_shape.h"

#include "segment_art.h"

#define A SEGMENT_ART_A
#define B SEGMENT_ART_B
#define C SEGMENT_ART_C
#define D SEGMENT_ART_D
#define E SEGMENT_ART_E
#define F SEGMENT_ART_F
#define G SEGMENT_ART_G
#define H SEGMENT_ART_H

// The month in ten steps, as seen from the northern hemisphere, where the
// moon waxes from the right.
#define STEPS 10
static const moon_shape_t shapes[STEPS] = {
    {0, 0, 0},                                                      // new
    {C | D, 0, A | B | C | D},                                      // thin waxing crescent
    {B | C | D, 0, A | B | C | D | H},                              // thick waxing crescent
    {B | C | D | H, 0, A | B | C | D | E | F},                      // first quarter
    {B | C | D | G | H, A | D | H, A | B | C | D},                  // waxing gibbous
    {A | B | C | D | E | F | G | H, A | D | E | F, A | B | C | D},  // full
    {A | E | F | G | H, A | D | E | F, A | D | H},                  // waning gibbous
    {A | E | F | H, A | B | C | D | E | F, 0},                      // third quarter
    {A | E | F, A | D | E | F | H, 0},                              // thick waning crescent
    {A | F, A | D | E | F, 0},                                      // thin waning crescent
};

moon_shape_t moon_shape(uint8_t phase, double fraction, bool southern) {
    uint8_t step;
    if (phase == 0 || phase > 7) step = 0;
    else if (phase == 1) step = fraction > 0.125 ? 2 : 1;
    else if (phase == 7) step = fraction < 0.875 ? 8 : 9;
    else step = phase + 1;

    // The southern sky shows the moon the other way up: its shape is the one
    // the north sees at the mirrored point of the month.
    if (southern && step != 0) step = STEPS - step;
    return shapes[step];
}

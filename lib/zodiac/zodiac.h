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

// Zodiac sign table and date lookup. No hardware dependencies, so it also
// builds on a PC for the host tests in test/.

#ifndef ZODIAC_H_
#define ZODIAC_H_

#include <stdint.h>

typedef struct {
    const char *name;          // 5 characters, for the custom LCD
    const char *abbreviation;  // 2 characters, for the classic LCD
    uint8_t start_month;       // 1-12
    uint8_t start_day;         // 1-31
} zodiac_sign_t;

#define ZODIAC_SIGN_COUNT 12

// Ordered by start date, Aquarius first.
extern const zodiac_sign_t zodiac_signs[ZODIAC_SIGN_COUNT];

// Index into zodiac_signs[] of the sign in effect on the given month (1-12)
// and day (1-31).
uint8_t zodiac_sign_for_date(uint8_t month, uint8_t day);

#endif // ZODIAC_H_

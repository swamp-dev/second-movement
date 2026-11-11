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

#include "zodiac.h"

const zodiac_sign_t zodiac_signs[ZODIAC_SIGN_COUNT] = {
    {"Aquar", "AQ", 1, 20},
    {"Pisce", "PI", 2, 19},
    {"Aries", "AR", 3, 21},
    {"Tauru", "TA", 4, 20},
    {"Gemin", "GE", 5, 21},
    {"Cance", "CN", 6, 21},
    {"Leo  ", "LE", 7, 23},
    {"Virgo", "VI", 8, 23},
    {"Libra", "LI", 9, 23},
    {"Scorp", "SC", 10, 23},
    {"Sagit", "SA", 11, 22},
    {"Capri", "CR", 12, 22},
};

uint8_t zodiac_sign_for_date(uint8_t month, uint8_t day) {
    // The sign in effect is the last one that started on or before this date.
    // Before Aquarius starts, Capricorn carries over from December.
    for (uint8_t i = 0; i < ZODIAC_SIGN_COUNT; i++) {
        if (month < zodiac_signs[i].start_month ||
            (month == zodiac_signs[i].start_month && day < zodiac_signs[i].start_day)) {
            return (i == 0) ? ZODIAC_SIGN_COUNT - 1 : i - 1;
        }
    }
    return ZODIAC_SIGN_COUNT - 1;
}

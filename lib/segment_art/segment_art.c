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

#include "segment_art.h"

#include <stddef.h>

#include "watch_slcd.h"
#include "watch_common_display.h"

#define COUNT(table) (sizeof(table) / sizeof((table)[0]))

void segment_art_draw(uint8_t position, uint8_t mask) {
    bool classic = watch_get_lcd_type() != WATCH_LCD_TYPE_CUSTOM;
    const digit_mapping_t *table = classic ? Classic_LCD_Display_Mapping : Custom_LCD_Display_Mapping;
    size_t positions = classic ? COUNT(Classic_LCD_Display_Mapping) : COUNT(Custom_LCD_Display_Mapping);
    if (position >= positions) return;
    const segment_mapping_t *segments = table[position].segment;

    for (int i = 0; i < 8; i++) {
        if (segments[i].value == segment_does_not_exist) continue;
        // On classic some segments share a pixel. Light it if any of them is
        // in the mask, and write it once so it never blinks off in between.
        bool lit = false;
        for (int j = 0; j < 8; j++) {
            if (segments[j].value == segments[i].value && (mask & (1 << j))) lit = true;
        }
        if (lit) watch_set_pixel(segments[i].address.com, segments[i].address.seg);
        else watch_clear_pixel(segments[i].address.com, segments[i].address.seg);
    }

    // The pixel watch_display_character adds to B, D and @ in classic position 0.
    if (classic && position == 0) watch_clear_pixel(0, 15);
}

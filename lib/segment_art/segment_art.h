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

// Draws shapes that aren't characters: a pattern of segments at one character
// position, on either LCD.

#ifndef SEGMENT_ART_H_
#define SEGMENT_ART_H_

#include <stdint.h>

// One bit per segment, in the order of the character sets and display
// mappings in watch_common_display.h. A-G are the usual seven segments:
//
//      A
//    F   B
//      G
//    E   C
//      D
//
// H is a position's eighth segment, where it has one.
#define SEGMENT_ART_A 0x01 // top bar
#define SEGMENT_ART_B 0x02 // upper right
#define SEGMENT_ART_C 0x04 // lower right
#define SEGMENT_ART_D 0x08 // bottom bar
#define SEGMENT_ART_E 0x10 // lower left
#define SEGMENT_ART_F 0x20 // upper left
#define SEGMENT_ART_G 0x40 // middle bar
#define SEGMENT_ART_H 0x80

// Lights the segments of `position` that are in `mask` and clears its other
// segments. Pixels of other positions are left alone. Positions are the
// indexes watch_display_character takes: 0-9 on classic, 0-10 on custom
// (10 is the third character of the top-left group). A position outside that
// range does nothing.
//
// What can't be drawn. A mask bit for a missing segment is ignored. Where
// several segments share one pixel, the pixel is lit if any of them is in
// the mask, so they can't be told apart.
//
// Custom LCD:
//   positions 2-7     no H
//   no shared pixels
//
// Classic LCD:
//   positions 3-9     no H
//   position 1        B and C are one pixel; E and F are one pixel
//   position 2        no F, no H; A, D and G are one pixel
//   position 4, 6     A and D are one pixel
//
// Classic position 0 also has a pixel outside its eight segments that
// watch_display_character lights for B, D and @. A draw at position 0 on
// classic turns it off, so a character drawn there before leaves nothing lit.
void segment_art_draw(uint8_t position, uint8_t mask);

#endif // SEGMENT_ART_H_

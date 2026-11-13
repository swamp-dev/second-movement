// Host stand-in for the segment LCD. It implements the pixel-level calls that
// the shared watch-library/shared/watch/watch_common_display.c draws through
// (watch_set_pixel, watch_clear_pixel, watch_clear_display,
// watch_get_lcd_type), keeps the pixels in memory, and reads them back as
// text. Link it with watch_common_display.c; the test owns main().
#ifndef FAKE_LCD_H_
#define FAKE_LCD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watch_slcd.h"

// Clears every pixel and switches the display to `type` (classic or custom),
// including the indicator layout that watch_discover_lcd_type sets on a watch.
void fake_lcd_reset(watch_lcd_type_t type);

// Number of character positions: 10 on classic, 11 on custom (index 10 is the
// third character of the custom top-left group).
uint8_t fake_lcd_num_positions(void);

bool fake_lcd_pixel(uint8_t com, uint8_t seg);

// True if the pixel is one of the (up to 8) segments the display mapping
// assigns to that position on the current LCD.
bool fake_lcd_in_mapping(uint8_t position, uint8_t com, uint8_t seg);

// The character shown at a position, '?' if no character draws that pattern
// (or if it shows a '?'). Several characters can draw the same pattern: 'S'
// and '5', 'O' and '0', and classic draws 'M' as 'n' in some positions. Then
// it prefers digits over letters, a letter or digit the display code draws as
// itself, then any letter or digit, then punctuation. So "SET" reads back
// "5Et"; assert with fake_lcd_shows when that matters.
// Classic position 1 (the second top character) shares a pixel between two
// segments, so many glyphs collapse there: 6, 8 and 9 all read back as '5'.
char fake_lcd_char(uint8_t position);

// The text shown in a location, in the order watch_display_text and
// watch_display_text_with_fallback fill it: TOP is 2 characters on classic
// and 5 on custom, TOP_LEFT 2 or 3, BOTTOM 6 (7 on custom when the leading
// "1" segment is lit), FULL 10 or 11 (FULL never shows the leading "1").
// Writes into buf, truncated to size - 1 characters; 13 bytes always fit.
const char *fake_lcd_text(watch_position_t location, char *buf, size_t size);

// True if the location shows exactly `text`, padded with spaces to the
// location's width (so "1" in HOURS means "1 "): every pixel that
// watch_display_text_with_fallback(location, padded, padded) would set or
// clear is in that state now. Pass the string for the current LCD: a face's
// long text on custom, its fallback on classic. Prefer this to comparing
// fake_lcd_text, because glyphs the LCD can't tell apart (S and 5) still
// match. Returns false, with a message on stderr, for text the display code
// can't draw there: empty, longer than the location (the custom bottom takes
// 7 when the first is '1'), or a character outside 0x20-0x7e.
bool fake_lcd_shows(watch_position_t location, const char *text);

bool fake_lcd_indicator(watch_indicator_t indicator);
bool fake_lcd_colon(void);
// Always false on classic, which has no decimal point.
bool fake_lcd_decimal(void);

#endif // FAKE_LCD_H_

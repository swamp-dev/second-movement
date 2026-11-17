// Screen captures for visual review. A shot_<face>.c drives one face on the
// harness, as a face test does, and calls shot() at every screen worth
// looking at. scripts/lcd-shots.py runs the program and paints each shot onto
// the simulator's LCD artwork (see CLAUDE.md, "Visual review").
//
// A shot is one line on stdout: the LCD type, the name, then every lit pixel
// as com:seg.
#ifndef SHOTS_H_
#define SHOTS_H_

#include <stdio.h>

#include "fake_lcd.h"

// `name` becomes the capture's file name: lowercase letters, digits, '-' and
// '_', and unique for the LCD type within the program.
static inline void shot(const char *name) {
    printf("%s\t%s\t", watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM ? "custom" : "classic", name);
    // The SLCD's whole address range, as in fake_lcd.c.
    for (int com = 0; com < 8; com++) {
        for (int seg = 0; seg < 64; seg++) {
            if (fake_lcd_pixel((uint8_t)com, (uint8_t)seg)) printf(" %d:%d", com, seg);
        }
    }
    printf("\n");
}

static const watch_lcd_type_t SHOT_LCDS[] = {WATCH_LCD_TYPE_CLASSIC, WATCH_LCD_TYPE_CUSTOM};
#define SHOT_NUM_LCDS (sizeof(SHOT_LCDS) / sizeof(SHOT_LCDS[0]))

#endif // SHOTS_H_

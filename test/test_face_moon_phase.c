// moon_phase_face (watch-faces/complication/moon_phase_face.c) on the face
// harness, on both LCD types: the text and the moon drawing for every shape
// the face draws, in both hemispheres, and stepping through a month with the
// buttons.

#include <stdio.h>

#include "check.h"
#include "harness.h"
#include "segment_art.h"
#include "watch_common_display.h"
#include "watch_utility.h"
#include "complication/moon_phase_face.h"

// The face's own constants: the first new moon of 2000 and the mean month.
#define FIRST_MOON 947182440
#define LUNAR_SECONDS (29.53058770576 * 86400)
// A lunation in March 2026.
#define LUNATION 324

#define A SEGMENT_ART_A
#define B SEGMENT_ART_B
#define C SEGMENT_ART_C
#define D SEGMENT_ART_D
#define E SEGMENT_ART_E
#define F SEGMENT_ART_F
#define G SEGMENT_ART_G
#define H SEGMENT_ART_H

static watch_lcd_type_t lcd;
static bool southern;

// The ten shapes the face tells apart, at a day of the lunar month well
// inside each: the eight phases, with each crescent split into a thin and a
// thick half. Text is for the northern hemisphere.
typedef struct {
    const char *name;
    double day;
    const char *bottom_custom;
    const char *bottom_classic;
    // The drawing in the northern hemisphere, as segments: classic position
    // 0, and custom positions 0 and 1, which form one disc.
    uint8_t classic;
    uint8_t custom_left, custom_right;
} shape_t;

static const shape_t shapes[] = {
    {"new",             0.5,  "NE!J  ", " Neu  ", 0, 0, 0},
    {"thin waxing",     2.5,  "CresNt", "CresNt", C | D, 0, A | B | C | D},
    {"thick waxing",    5.0,  "CresNt", "CresNt", B | C | D, 0, A | B | C | D | H},
    {"first quarter",   7.4,  "1stQtr", " 1st q", B | C | D | H, 0, A | B | C | D | E | F},
    {"waxing gibbous",  11.0, "GbboUs", " Gibb ", B | C | D | G | H, A | D | H, A | B | C | D},
    {"full",            14.8, "FULL  ", " FULL ", A | B | C | D | E | F | G | H, A | D | E | F, A | B | C | D},
    {"waning gibbous",  18.5, "GbboUs", " Gibb ", A | E | F | G | H, A | D | E | F, A | D | H},
    {"third quarter",   22.1, "3rdQtr", " 3rd q", A | E | F | H, A | B | C | D | E | F, 0},
    {"thick waning",    24.5, "CresNt", "CresNt", A | E | F, A | D | E | F | H, 0},
    {"thin waning",     27.2, "CresNt", "CresNt", A | F, A | D | E | F, 0},
};
#define NUM_SHAPES (sizeof(shapes) / sizeof(shapes[0]))

// The shape drawn in the southern hemisphere is the one the north sees at
// the mirrored point of the month. On custom, where the drawing is
// symmetric, that is also its mirror image.
static const shape_t *mirrored(size_t shape) {
    return &shapes[shape == 0 ? 0 : NUM_SHAPES - shape];
}

// The row with the bottom text: the quarters swap names in the south.
static const shape_t *named_as(size_t shape) {
    if (southern && (shape == 3 || shape == 7)) return &shapes[NUM_SHAPES - shape];
    return &shapes[shape];
}

static watch_date_time_t at_day(double day) {
    uint32_t timestamp = (uint32_t)(FIRST_MOON + LUNATION * LUNAR_SECONDS + day * 86400);
    return watch_utility_date_time_from_unix_time(timestamp, 0);
}

static void start_face(double day) {
    harness_init(lcd, at_day(day), harness_default_settings());
    if (southern) {
        movement_location_t sydney = {.bit = {.latitude = -3387, .longitude = 15121}};
        harness_write_file("location.u32", &sydney.reg, sizeof(sydney.reg));
    }
    harness_load_face(&moon_phase_face);
}

// The lit segments of a position, as a segment_art mask.
static uint8_t segments_at(uint8_t position) {
    digit_mapping_t segmap = lcd == WATCH_LCD_TYPE_CUSTOM ? Custom_LCD_Display_Mapping[position]
                                                          : Classic_LCD_Display_Mapping[position];
    uint8_t mask = 0;
    for (int i = 0; i < 8; i++) {
        if (segmap.segment[i].value == segment_does_not_exist) continue;
        if (fake_lcd_pixel(segmap.segment[i].address.com, segmap.segment[i].address.seg)) mask |= 1 << i;
    }
    return mask;
}

// Everything on screen for `shape`, with `day_of_month` in the top right.
static void check_screen(size_t shape, uint8_t day_of_month) {
    const shape_t *name = named_as(shape);
    const shape_t *drawn = southern ? mirrored(shape) : &shapes[shape];
    char day[8];
    snprintf(day, sizeof(day), "%2d", day_of_month);
    CHECK(fake_lcd_shows(WATCH_POSITION_TOP_RIGHT, day));

    int failures = check_failures;
    if (lcd == WATCH_LCD_TYPE_CUSTOM) {
        CHECK_EQ_INT(segments_at(0), drawn->custom_left, "the moon's left half");
        CHECK_EQ_INT(segments_at(1), drawn->custom_right, "the moon's right half");
        CHECK_EQ_INT(segments_at(10), 0, "nothing beside the moon");
        CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, name->bottom_custom));
    } else {
        CHECK_EQ_INT(segments_at(0), drawn->classic, "the moon");
        CHECK(!fake_lcd_pixel(0, 15));  // position 0's ninth pixel
        CHECK_EQ_INT(segments_at(1), 0, "nothing beside the moon");
        CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, name->bottom_classic));
    }
    if (check_failures != failures) {
        fprintf(stderr, "(the failures above: %s, %s hemisphere, %s LCD)\n", shapes[shape].name,
                southern ? "southern" : "northern", lcd == WATCH_LCD_TYPE_CUSTOM ? "custom" : "classic");
    }
}

static void test_every_shape(void) {
    for (size_t i = 0; i < NUM_SHAPES; i++) {
        start_face(shapes[i].day);
        check_screen(i, at_day(shapes[i].day).unit.day);
    }
}

// From half a day after the new moon, a day per press: the shape on each of
// the 31 days, as indexes into shapes[].
static const uint8_t month[] = {0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5,
                                6, 6, 6, 6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 0, 0};
#define MONTH_DAYS (sizeof(month) / sizeof(month[0]))

// ALARM steps a day forward and LIGHT a day back. Each screen is checked in
// full, so a segment left over from the day before would show.
static void test_step_through_month(void) {
    start_face(0.5);
    for (size_t i = 0; i < MONTH_DAYS; i++) {
        if (i > 0) harness_press(BUTTON_ALARM, PRESS_SHORT);
        check_screen(month[i], at_day(0.5 + i).unit.day);
    }
    for (size_t i = MONTH_DAYS - 1; i-- > 0;) {
        harness_press(BUTTON_LIGHT, PRESS_SHORT);
        check_screen(month[i], at_day(0.5 + i).unit.day);
    }
}

static void test_long_alarm_returns_to_today(void) {
    start_face(14.8);
    for (int i = 0; i < 10; i++) harness_press(BUTTON_ALARM, PRESS_SHORT);
    check_screen(8, at_day(24.8).unit.day);
    harness_press(BUTTON_ALARM, PRESS_LONG);
    check_screen(5, at_day(14.8).unit.day);
}

int main(void) {
    const watch_lcd_type_t types[] = {WATCH_LCD_TYPE_CLASSIC, WATCH_LCD_TYPE_CUSTOM};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        for (int south = 0; south <= 1; south++) {
            lcd = types[i];
            southern = south;
            test_every_shape();
            test_step_through_month();
            test_long_alarm_returns_to_today();
        }
    }
    return check_summary("test_face_moon_phase");
}

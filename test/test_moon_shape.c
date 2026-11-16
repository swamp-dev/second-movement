// Tests for lib/moon_shape: the segments that draw the moon for each phase,
// crescent half and hemisphere.
#include <stdio.h>

#include "check.h"
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

// A fraction of the lunar month inside each phase the face tells apart
// (its phase_changes table, in days of 29.53).
static const double fraction_of[] = {0.02, 0.2, 0.25, 0.4, 0.5, 0.6, 0.75, 0.8, 0.98};

static bool same(moon_shape_t a, moon_shape_t b) {
    return a.classic == b.classic && a.custom_left == b.custom_left && a.custom_right == b.custom_right;
}

static void check_shape(moon_shape_t shape, uint8_t classic, uint8_t left, uint8_t right, const char *what) {
    CHECK_EQ_INT(shape.classic, classic, "%s, classic", what);
    CHECK_EQ_INT(shape.custom_left, left, "%s, custom left", what);
    CHECK_EQ_INT(shape.custom_right, right, "%s, custom right", what);
}

// The custom drawing is a disc across two characters, drawn as the outline
// of its lit part: the right character alone up to the first quarter, then
// growing leftwards.
static void test_northern(void) {
    check_shape(moon_shape(0, 0.02, false), 0, 0, 0, "new");
    check_shape(moon_shape(8, 0.98, false), 0, 0, 0, "new, end of the month");
    check_shape(moon_shape(1, 0.05, false), C | D, 0, A | B | C | D, "thin waxing crescent");
    check_shape(moon_shape(1, 0.2, false), B | C | D, 0, A | B | C | D | H, "thick waxing crescent");
    check_shape(moon_shape(2, 0.25, false), B | C | D | H, 0, A | B | C | D | E | F, "first quarter");
    check_shape(moon_shape(3, 0.4, false), B | C | D | G | H, A | D | H, A | B | C | D, "waxing gibbous");
    check_shape(moon_shape(4, 0.5, false), 0xFF, A | D | E | F, A | B | C | D, "full");
    check_shape(moon_shape(5, 0.6, false), A | E | F | G | H, A | D | E | F, A | D | H, "waning gibbous");
    check_shape(moon_shape(6, 0.75, false), A | E | F | H, A | B | C | D | E | F, 0, "third quarter");
    check_shape(moon_shape(7, 0.8, false), A | E | F, A | D | E | F | H, 0, "thick waning crescent");
    check_shape(moon_shape(7, 0.95, false), A | F, A | D | E | F, 0, "thin waning crescent");
}

// The crescents change at an eighth of the month from the new moon.
static void test_crescent_halves(void) {
    CHECK(same(moon_shape(1, 0.125, false), moon_shape(1, 0.05, false)));
    CHECK(same(moon_shape(1, 0.126, false), moon_shape(1, 0.2, false)));
    CHECK(same(moon_shape(7, 0.874, false), moon_shape(7, 0.8, false)));
    CHECK(same(moon_shape(7, 0.875, false), moon_shape(7, 0.95, false)));
}

static void test_phases_differ(void) {
    for (int south = 0; south <= 1; south++) {
        for (uint8_t a = 0; a < 8; a++) {
            for (uint8_t b = a + 1; b < 8; b++) {
                moon_shape_t one = moon_shape(a, fraction_of[a], south);
                moon_shape_t other = moon_shape(b, fraction_of[b], south);
                CHECK_EQ_INT(one.classic == other.classic, 0, "classic, phases %d and %d", a, b);
                CHECK_EQ_INT(one.custom_left == other.custom_left && one.custom_right == other.custom_right, 0,
                             "custom, phases %d and %d", a, b);
            }
        }
    }
}

// A character's segments seen in a mirror: B and F swap, and C and E.
static uint8_t flipped(uint8_t mask) {
    uint8_t out = mask & (A | D | G | H);
    if (mask & B) out |= F;
    if (mask & F) out |= B;
    if (mask & C) out |= E;
    if (mask & E) out |= C;
    return out;
}

// South of the equator the moon is seen the other way up, so the lit side
// swaps. On custom that is the mirror image. On classic it is the shape the
// north has at the mirrored point of the month, as the face has always drawn.
static void test_southern_mirrors(void) {
    const double fractions[] = {0.02, 0.05, 0.2, 0.25, 0.4, 0.5, 0.6, 0.75, 0.8, 0.95};
    const uint8_t phases[] = {0, 1, 1, 2, 3, 4, 5, 6, 7, 7};
    for (int i = 0; i < 10; i++) {
        moon_shape_t north = moon_shape(phases[i], fractions[i], false);
        moon_shape_t south = moon_shape(phases[i], fractions[i], true);
        CHECK_EQ_INT(south.custom_left, flipped(north.custom_right), "left, step %d", i);
        CHECK_EQ_INT(south.custom_right, flipped(north.custom_left), "right, step %d", i);
        int j = i == 0 ? 0 : 10 - i;
        CHECK_EQ_INT(south.classic, moon_shape(phases[j], fractions[j], false).classic, "classic, step %d", i);
    }
}

// The face's phase search runs off the end of its table (index 9) at the
// exact second of a new moon.
static void test_phase_out_of_range_is_new(void) {
    check_shape(moon_shape(9, 0.0, false), 0, 0, 0, "phase 9");
    check_shape(moon_shape(255, 0.5, true), 0, 0, 0, "phase 255");
}

int main(void) {
    test_northern();
    test_crescent_halves();
    test_phases_differ();
    test_southern_mirrors();
    test_phase_out_of_range_is_new();
    return check_summary("test_moon_shape");
}

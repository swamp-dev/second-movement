// Tests for lib/segment_art through the fake LCD in fakes/, on both displays:
// every position in the mapping table, every mask.
#include <string.h>

#include "check.h"
#include "fake_lcd.h"
#include "segment_art.h"
#include "watch_common_display.h"

#define COMS 8
#define SEGS 64

typedef bool pixels_t[COMS][SEGS];

static const watch_lcd_type_t lcd_types[] = {WATCH_LCD_TYPE_CLASSIC, WATCH_LCD_TYPE_CUSTOM};

static const char *lcd_name(void) {
    return watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM ? "custom" : "classic";
}

static digit_mapping_t mapping(uint8_t position) {
    return watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM ? Custom_LCD_Display_Mapping[position]
                                                         : Classic_LCD_Display_Mapping[position];
}

static void snapshot(pixels_t out) {
    for (uint8_t com = 0; com < COMS; com++)
        for (uint8_t seg = 0; seg < SEGS; seg++)
            out[com][seg] = fake_lcd_pixel(com, seg);
}

static void light_everything(void) {
    for (uint8_t com = 0; com < COMS; com++)
        for (uint8_t seg = 0; seg < SEGS; seg++)
            watch_set_pixel(com, seg);
}

// What the screen must hold after segment_art_draw(position, mask) over
// `before`: a pixel of the position is lit if a segment in the mask maps to
// it, the classic position-0 extra pixel is off, and the rest is untouched.
static void expected_after(const pixels_t before, uint8_t position, uint8_t mask, pixels_t out) {
    memcpy(out, before, sizeof(pixels_t));
    digit_mapping_t segmap = mapping(position);
    for (int i = 0; i < 8; i++) {
        if (segmap.segment[i].value == segment_does_not_exist) continue;
        out[segmap.segment[i].address.com][segmap.segment[i].address.seg] = false;
    }
    for (int i = 0; i < 8; i++) {
        if (segmap.segment[i].value == segment_does_not_exist) continue;
        if (mask & (1 << i)) out[segmap.segment[i].address.com][segmap.segment[i].address.seg] = true;
    }
    if (watch_get_lcd_type() == WATCH_LCD_TYPE_CLASSIC && position == 0) out[0][15] = false;
}

static int differences(const pixels_t expected) {
    int n = 0;
    for (uint8_t com = 0; com < COMS; com++)
        for (uint8_t seg = 0; seg < SEGS; seg++)
            n += fake_lcd_pixel(com, seg) != expected[com][seg];
    return n;
}

// Every mask at every position, over a blank screen and over a fully lit one.
// The lit screen shows that segments left out of the mask are cleared and
// that no pixel outside the position changes.
static void test_every_mask_at_every_position(void) {
    for (size_t t = 0; t < 2; t++) {
        fake_lcd_reset(lcd_types[t]);
        uint8_t positions = fake_lcd_num_positions();
        for (int lit = 0; lit < 2; lit++) {
            for (uint8_t position = 0; position < positions; position++) {
                for (int mask = 0; mask < 256; mask++) {
                    fake_lcd_reset(lcd_types[t]);
                    if (lit) light_everything();
                    pixels_t before, expected;
                    snapshot(before);
                    expected_after(before, position, (uint8_t)mask, expected);
                    segment_art_draw(position, (uint8_t)mask);
                    CHECK_EQ_INT(differences(expected), 0, "%s position %d mask 0x%02x over a %s screen",
                                 lcd_name(), position, mask, lit ? "lit" : "blank");
                }
            }
        }
    }
}

static int lit_pixels(void) {
    int n = 0;
    for (uint8_t com = 0; com < COMS; com++)
        for (uint8_t seg = 0; seg < SEGS; seg++)
            n += fake_lcd_pixel(com, seg);
    return n;
}

// A few addresses written out by hand, so the test doesn't rest only on a
// second copy of the lookup.
static void test_known_addresses(void) {
    fake_lcd_reset(WATCH_LCD_TYPE_CUSTOM);
    segment_art_draw(0, SEGMENT_ART_A);
    CHECK(fake_lcd_pixel(0, 19));
    CHECK_EQ_INT(lit_pixels(), 1, "custom 0A");
    segment_art_draw(9, SEGMENT_ART_D | SEGMENT_ART_G | SEGMENT_ART_H);
    CHECK(fake_lcd_pixel(0, 7));
    CHECK(fake_lcd_pixel(1, 6));
    CHECK(fake_lcd_pixel(1, 7));
    CHECK_EQ_INT(lit_pixels(), 4, "custom 0A and 9D, 9G, 9H");
    segment_art_draw(10, SEGMENT_ART_B);
    CHECK(fake_lcd_pixel(2, 12));

    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    segment_art_draw(3, SEGMENT_ART_A | SEGMENT_ART_D);
    CHECK(fake_lcd_pixel(0, 7));
    CHECK(fake_lcd_pixel(2, 6));
    CHECK_EQ_INT(lit_pixels(), 2, "classic 3A and 3D");
}

static void test_second_draw_clears_dropped_segments(void) {
    for (size_t t = 0; t < 2; t++) {
        fake_lcd_reset(lcd_types[t]);
        for (uint8_t position = 0; position < fake_lcd_num_positions(); position++) {
            segment_art_draw(position, 0xff);
            CHECK(lit_pixels() > 0);
            segment_art_draw(position, 0);
            CHECK_EQ_INT(lit_pixels(), 0, "%s position %d", lcd_name(), position);
        }
    }
    fake_lcd_reset(WATCH_LCD_TYPE_CUSTOM);
    segment_art_draw(4, SEGMENT_ART_A | SEGMENT_ART_G | SEGMENT_ART_D);
    segment_art_draw(4, SEGMENT_ART_G);
    CHECK(fake_lcd_pixel(2, 22));
    CHECK_EQ_INT(lit_pixels(), 1, "custom 4G alone");
}

// Classic positions where one pixel serves several segments: the pixel is lit
// if any of them is in the mask, whatever their order.
static void test_shared_pixels(void) {
    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    segment_art_draw(1, SEGMENT_ART_B); // 1B and 1C are both (1,11)
    CHECK(fake_lcd_pixel(1, 11));
    segment_art_draw(1, SEGMENT_ART_C);
    CHECK(fake_lcd_pixel(1, 11));
    segment_art_draw(1, SEGMENT_ART_E); // 1E and 1F are both (1,12)
    CHECK(fake_lcd_pixel(1, 12));
    CHECK(!fake_lcd_pixel(1, 11));
    segment_art_draw(2, SEGMENT_ART_A); // 2A, 2D and 2G are all (1,9)
    CHECK(fake_lcd_pixel(1, 9));
    segment_art_draw(2, SEGMENT_ART_D);
    CHECK(fake_lcd_pixel(1, 9));
    segment_art_draw(2, SEGMENT_ART_B);
    CHECK(!fake_lcd_pixel(1, 9));
}

// segment_does_not_exist is 0xff, which read as an address is pixel (3, 63).
static void test_missing_segments_are_skipped(void) {
    for (size_t t = 0; t < 2; t++) {
        for (uint8_t position = 0;; position++) {
            fake_lcd_reset(lcd_types[t]);
            if (position >= fake_lcd_num_positions()) break;
            segment_art_draw(position, 0xff);
            CHECK(!fake_lcd_pixel(3, 63));
            light_everything();
            segment_art_draw(position, 0);
            CHECK(fake_lcd_pixel(3, 63));
        }
    }
    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    segment_art_draw(2, SEGMENT_ART_F | SEGMENT_ART_H); // classic position 2 has neither
    CHECK_EQ_INT(lit_pixels(), 0, "classic 2F and 2H");
}

// watch_display_character lights an extra pixel, (0,15), for B, D and @ in
// classic position 0. It is in no mapping, so a draw has to clear it itself.
static void test_classic_position_0_extra_pixel(void) {
    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    watch_display_text(WATCH_POSITION_TOP_LEFT, "B ");
    CHECK(fake_lcd_pixel(0, 15));
    segment_art_draw(0, SEGMENT_ART_G);
    CHECK(!fake_lcd_pixel(0, 15));
    CHECK_EQ_INT(lit_pixels(), 1, "classic 0G alone after a B");

    // On custom (0,15) is segment D of position 5; position 0 leaves it alone.
    fake_lcd_reset(WATCH_LCD_TYPE_CUSTOM);
    segment_art_draw(5, SEGMENT_ART_D);
    segment_art_draw(0, SEGMENT_ART_G);
    CHECK(fake_lcd_pixel(0, 15));
}

static void test_out_of_range_position_does_nothing(void) {
    for (size_t t = 0; t < 2; t++) {
        for (int lit = 0; lit < 2; lit++) {
            fake_lcd_reset(lcd_types[t]);
            if (lit) light_everything();
            pixels_t before;
            snapshot(before);
            for (int position = fake_lcd_num_positions(); position < 256; position++) {
                segment_art_draw((uint8_t)position, 0xff);
                segment_art_draw((uint8_t)position, 0);
            }
            CHECK_EQ_INT(differences(before), 0, "%s, %s screen", lcd_name(), lit ? "lit" : "blank");
        }
    }
}

int main(void) {
    test_every_mask_at_every_position();
    test_known_addresses();
    test_second_draw_clears_dropped_segments();
    test_shared_pixels();
    test_missing_segments_are_skipped();
    test_classic_position_0_extra_pixel();
    test_out_of_range_position_does_nothing();
    return check_summary("test_segment_art");
}

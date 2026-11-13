// Tests for the shared display code (watch-library/shared/watch/
// watch_common_display.c) through the fake LCD in fakes/, on both displays.
//
// Not exercised: 7-character bottom strings on classic, which read past
// Classic_LCD_Display_Mapping (#60) and would trip ASan.
#include <string.h>

#include "check.h"
#include "fake_lcd.h"

void watch_display_character(uint8_t character, uint8_t position);

#define FIRST_CHAR 0x20
#define LAST_CHAR 0x7e

static const watch_lcd_type_t lcd_types[] = {WATCH_LCD_TYPE_CLASSIC, WATCH_LCD_TYPE_CUSTOM};

static const char *lcd_name(watch_lcd_type_t type) {
    return type == WATCH_LCD_TYPE_CUSTOM ? "custom" : "classic";
}

static int lit_pixels(void) {
    int n = 0;
    for (uint8_t com = 0; com < 8; com++)
        for (uint8_t seg = 0; seg < 64; seg++)
            n += fake_lcd_pixel(com, seg);
    return n;
}

static bool in_any_mapping(uint8_t com, uint8_t seg) {
    for (uint8_t p = 0; p < fake_lcd_num_positions(); p++)
        if (fake_lcd_in_mapping(p, com, seg)) return true;
    return false;
}

static bool mapping_pixels_equal(uint8_t position, const bool before[8][64]) {
    for (uint8_t com = 0; com < 8; com++)
        for (uint8_t seg = 0; seg < 64; seg++)
            if (fake_lcd_in_mapping(position, com, seg) && fake_lcd_pixel(com, seg) != before[com][seg])
                return false;
    return true;
}

static void snapshot(bool out[8][64]) {
    for (uint8_t com = 0; com < 8; com++)
        for (uint8_t seg = 0; seg < 64; seg++)
            out[com][seg] = fake_lcd_pixel(com, seg);
}

static void check_text(watch_position_t location, const char *expected, const char *what) {
    char buf[16];
    fake_lcd_text(location, buf, sizeof(buf));
    if (strcmp(buf, expected) != 0) {
        check_count++;
        check_failures++;
        fprintf(stderr, "%s:%d: %s on %s: shows \"%s\", expected \"%s\"\n", __FILE__, __LINE__, what,
                lcd_name(watch_get_lcd_type()), buf, expected);
    } else {
        check_count++;
    }
}

// Every character the display can be asked to draw decodes to something that
// draws the same mapped segments and reads back as itself. This checks the
// decoder is consistent; check_round_trips and test_locations check it reads
// the right character.
static void test_decoder_is_consistent(void) {
    for (size_t t = 0; t < 2; t++) {
        fake_lcd_reset(lcd_types[t]);
        for (uint8_t pos = 0; pos < fake_lcd_num_positions(); pos++) {
            for (int c = FIRST_CHAR; c <= LAST_CHAR; c++) {
                fake_lcd_reset(lcd_types[t]);
                watch_display_character((uint8_t)c, pos);
                bool drawn[8][64];
                snapshot(drawn);
                char decoded = fake_lcd_char(pos);
                // '?' is also the decoder's "no match", so only '?' may decode to it.
                CHECK_EQ_INT(decoded != '?' || c == '?', 1, "%s pos %d '%c' decodes to '?'", lcd_name(lcd_types[t]), pos, c);
                if (decoded == '?' && c != '?') continue;
                fake_lcd_reset(lcd_types[t]);
                watch_display_character((uint8_t)decoded, pos);
                CHECK_EQ_INT(mapping_pixels_equal(pos, drawn), 1, "%s pos %d '%c' decodes to '%c', which draws differently",
                             lcd_name(lcd_types[t]), pos, c, decoded);
                CHECK_EQ_INT(fake_lcd_char(pos), decoded, "%s pos %d: '%c' doesn't decode as itself",
                             lcd_name(lcd_types[t]), pos, decoded);
            }
        }
    }
}

static void check_round_trips(uint8_t pos, const char *chars) {
    for (const char *c = chars; *c; c++) {
        fake_lcd_reset(watch_get_lcd_type());
        watch_display_character((uint8_t)*c, pos);
        CHECK_EQ_INT(fake_lcd_char(pos), *c, "%s pos %d draws '%c' as '%c'", lcd_name(watch_get_lcd_type()), pos, *c,
                     fake_lcd_char(pos));
    }
}

static void check_not_drawable(uint8_t pos, const char *chars) {
    for (const char *c = chars; *c; c++) {
        fake_lcd_reset(watch_get_lcd_type());
        watch_display_character((uint8_t)*c, pos);
        CHECK(fake_lcd_char(pos) != *c);
    }
}

static void test_digits_and_letters(void) {
    // Custom: every position draws every digit and these letters as themselves.
    fake_lcd_reset(WATCH_LCD_TYPE_CUSTOM);
    for (uint8_t pos = 0; pos < 11; pos++) check_round_trips(pos, "0123456789ACEFHJLPUbcdhnoru");
    // ...including the ones classic can't draw in some positions.
    check_round_trips(1, "G");
    check_round_trips(4, "bdY");
    check_round_trips(6, "bdY");

    // Classic: the time digits draw every digit; the date's tens digit
    // (position 2) only needs 0-3.
    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    check_round_trips(0, "0123456789");
    for (uint8_t pos = 3; pos < 10; pos++) check_round_trips(pos, "0123456789");
    check_round_trips(2, "0123");
    // Known classic limits: the second top character (position 1) shares a
    // pixel between two segments, so it can't draw G, 6, 8 or 9 (all three
    // digits read back as 5), and bottom characters 0 and 2 (positions 4
    // and 6) can't draw b, d or Y.
    check_not_drawable(1, "G689");
    check_round_trips(1, "0123457");
    for (const char *c = "689"; *c; c++) {
        fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
        watch_display_character((uint8_t)*c, 1);
        CHECK_EQ_INT(fake_lcd_char(1), '5', "classic pos 1 '%c'", *c);
    }
    check_not_drawable(4, "bdY");
    check_not_drawable(6, "bdY");
}

static void test_locations(void) {
    for (size_t t = 0; t < 2; t++) {
        fake_lcd_reset(lcd_types[t]);
        watch_display_text(WATCH_POSITION_BOTTOM, "123456");
        check_text(WATCH_POSITION_BOTTOM, "123456", "BOTTOM");
        watch_display_text(WATCH_POSITION_HOURS, "09");
        watch_display_text(WATCH_POSITION_MINUTES, "41");
        watch_display_text(WATCH_POSITION_SECONDS, "27");
        check_text(WATCH_POSITION_BOTTOM, "094127", "HOURS/MINUTES/SECONDS");
        check_text(WATCH_POSITION_HOURS, "09", "HOURS");
        check_text(WATCH_POSITION_MINUTES, "41", "MINUTES");
        check_text(WATCH_POSITION_SECONDS, "27", "SECONDS");
        watch_display_text(WATCH_POSITION_TOP_RIGHT, "30");
        check_text(WATCH_POSITION_TOP_RIGHT, "30", "TOP_RIGHT");
    }

    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    watch_display_text(WATCH_POSITION_TOP_LEFT, "AC");
    check_text(WATCH_POSITION_TOP_LEFT, "AC", "TOP_LEFT");
    check_text(WATCH_POSITION_TOP, "AC", "TOP");
    watch_display_text(WATCH_POSITION_FULL, "AC10345678");
    check_text(WATCH_POSITION_FULL, "AC10345678", "FULL");

    fake_lcd_reset(WATCH_LCD_TYPE_CUSTOM);
    watch_display_text_with_fallback(WATCH_POSITION_TOP, "AC 12", "AC");
    check_text(WATCH_POSITION_TOP, "AC 12", "TOP");
    check_text(WATCH_POSITION_TOP_RIGHT, "12", "TOP_RIGHT after TOP");
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "ACE", "AC");
    check_text(WATCH_POSITION_TOP_LEFT, "ACE", "TOP_LEFT");
    watch_display_text(WATCH_POSITION_FULL, "AC103456789");
    check_text(WATCH_POSITION_FULL, "AC103456789", "FULL");
}

static void test_fallback(void) {
    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "ACE", "HJ");
    check_text(WATCH_POSITION_TOP_LEFT, "HJ", "classic uses the fallback");
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "1234567", "234567");
    check_text(WATCH_POSITION_BOTTOM, "234567", "classic bottom fallback");

    fake_lcd_reset(WATCH_LCD_TYPE_CUSTOM);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "ACE", "HJ");
    check_text(WATCH_POSITION_TOP_LEFT, "ACE", "custom uses the long text");
    // A 7-character bottom string starting with 1 lights the leading half-digit.
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "1234567", "234567");
    check_text(WATCH_POSITION_BOTTOM, "1234567", "custom leading 1");
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "234567", "234567");
    check_text(WATCH_POSITION_BOTTOM, "234567", "custom leading 1 cleared");
}

static void test_shows(void) {
    for (size_t t = 0; t < 2; t++) {
        fake_lcd_reset(lcd_types[t]);
        watch_display_text(WATCH_POSITION_BOTTOM, "SEt 12");
        CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, "SEt 12"));
        CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, "5Et 12")); // S and 5 are the same glyph
        CHECK(!fake_lcd_shows(WATCH_POSITION_BOTTOM, "SEt 13"));
        CHECK(fake_lcd_shows(WATCH_POSITION_SECONDS, "12"));
        CHECK(!fake_lcd_shows(WATCH_POSITION_HOURS, "12"));
        // Shorter text is padded with spaces, so it can't match a prefix.
        CHECK(!fake_lcd_shows(WATCH_POSITION_SECONDS, "1"));
        CHECK(!fake_lcd_shows(WATCH_POSITION_BOTTOM, "SEt"));
        watch_display_text(WATCH_POSITION_SECONDS, "1 ");
        CHECK(fake_lcd_shows(WATCH_POSITION_SECONDS, "1"));
        // Text the display code can't draw is refused, not replayed.
        CHECK(!fake_lcd_shows(WATCH_POSITION_SECONDS, ""));
        CHECK(!fake_lcd_shows(WATCH_POSITION_SECONDS, "123"));
        CHECK(!fake_lcd_shows(WATCH_POSITION_SECONDS, "1\t"));
    }
    fake_lcd_reset(WATCH_LCD_TYPE_CUSTOM);
    watch_display_text_with_fallback(WATCH_POSITION_TOP, "SUNRS", "SR");
    CHECK(fake_lcd_shows(WATCH_POSITION_TOP, "SUNRS"));
    CHECK(!fake_lcd_shows(WATCH_POSITION_TOP, "SUNSE"));
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "1234567", "234567");
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, "1234567"));
    CHECK(!fake_lcd_shows(WATCH_POSITION_BOTTOM, "234567"));

    // On classic a 7-character bottom string would read past the position
    // table (#60), so it's refused.
    fake_lcd_reset(WATCH_LCD_TYPE_CLASSIC);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "1234567", "234567");
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, "234567"));
    CHECK(!fake_lcd_shows(WATCH_POSITION_BOTTOM, "1234567"));
}

static void test_indicators(void) {
    for (size_t t = 0; t < 2; t++) {
        watch_lcd_type_t type = lcd_types[t];
        watch_indicator_t last = type == WATCH_LCD_TYPE_CUSTOM ? WATCH_INDICATOR_SLEEP : WATCH_INDICATOR_LAP;
        for (watch_indicator_t ind = WATCH_INDICATOR_SIGNAL; ind <= last; ind++) {
            fake_lcd_reset(type);
            watch_set_indicator(ind);
            CHECK_EQ_INT(lit_pixels(), 1, "%s indicator %d", lcd_name(type), ind);
            for (watch_indicator_t other = WATCH_INDICATOR_SIGNAL; other <= WATCH_INDICATOR_SLEEP; other++)
                CHECK_EQ_INT(fake_lcd_indicator(other), other == ind, "%s set %d, read %d", lcd_name(type), ind, other);
            for (uint8_t com = 0; com < 8; com++)
                for (uint8_t seg = 0; seg < 64; seg++)
                    if (fake_lcd_pixel(com, seg))
                        CHECK_EQ_INT(in_any_mapping(com, seg), 0, "%s indicator %d is a digit segment", lcd_name(type), ind);
            watch_clear_indicator(ind);
            CHECK_EQ_INT(lit_pixels(), 0, "%s clear indicator %d", lcd_name(type), ind);
        }
        fake_lcd_reset(type);
        for (watch_indicator_t ind = WATCH_INDICATOR_SIGNAL; ind <= last; ind++) watch_set_indicator(ind);
        watch_clear_all_indicators();
        CHECK_EQ_INT(lit_pixels(), 0, "%s clear all indicators", lcd_name(type));

        fake_lcd_reset(type);
        watch_set_colon();
        CHECK(fake_lcd_colon());
        CHECK_EQ_INT(lit_pixels(), 1, "%s colon", lcd_name(type));
        watch_clear_colon();
        CHECK(!fake_lcd_colon());

        fake_lcd_reset(type);
        watch_set_decimal_if_available();
        CHECK_EQ_INT(fake_lcd_decimal(), type == WATCH_LCD_TYPE_CUSTOM, "%s decimal", lcd_name(type));
        CHECK_EQ_INT(lit_pixels(), type == WATCH_LCD_TYPE_CUSTOM, "%s decimal pixels", lcd_name(type));
        watch_clear_decimal_if_available();
        CHECK(!fake_lcd_decimal());
    }
}

// Known cross-talk: on custom, the classic-only extra segments in the top-left
// positions land on other characters' segments (#116). Remove with its fix.
static bool known_crosstalk(watch_lcd_type_t type, uint8_t pos, int c) {
    if (type != WATCH_LCD_TYPE_CUSTOM) return false;
    if (pos == 0) return c == 'B' || c == 'D' || c == '@';
    if (pos == 1) return c == 'T' || c == 'B' || c == 'D' || c == '@';
    return false;
}

// A pixel a draw at this position may change: its own segments, plus the
// extra segment classic position 0 uses for B, D and @.
static bool may_change(watch_lcd_type_t type, uint8_t pos, uint8_t com, uint8_t seg) {
    if (fake_lcd_in_mapping(pos, com, seg)) return true;
    return type == WATCH_LCD_TYPE_CLASSIC && pos == 0 && com == 0 && seg == 15;
}

// Drawing a character changes no pixel outside its own position (another
// character, an indicator, the colon, the decimal point, the leading 1),
// whether the screen started blank or fully lit. Each case on the known list
// must still cross-talk, so a fix of #116 fails here until the list shrinks.
static void test_no_crosstalk(void) {
    for (size_t t = 0; t < 2; t++) {
        watch_lcd_type_t type = lcd_types[t];
        fake_lcd_reset(type);
        for (uint8_t pos = 0; pos < fake_lcd_num_positions(); pos++) {
            for (int c = FIRST_CHAR; c <= LAST_CHAR; c++) {
                bool crossed = false;
                for (int lit = 0; lit < 2; lit++) {
                    fake_lcd_reset(type);
                    if (lit)
                        for (uint8_t com = 0; com < 8; com++)
                            for (uint8_t seg = 0; seg < 64; seg++) watch_set_pixel(com, seg);
                    bool before[8][64];
                    snapshot(before);
                    watch_display_character((uint8_t)c, pos);
                    for (uint8_t com = 0; com < 8; com++)
                        for (uint8_t seg = 0; seg < 64; seg++)
                            if (!may_change(type, pos, com, seg) && fake_lcd_pixel(com, seg) != before[com][seg]) {
                                crossed = true;
                                if (!known_crosstalk(type, pos, c))
                                    fprintf(stderr, "%s: '%c' at position %d changes pixel (%d,%d)\n", lcd_name(type), c,
                                            pos, com, seg);
                            }
                }
                CHECK_EQ_INT(crossed, known_crosstalk(type, pos, c), "%s '%c' at position %d cross-talk", lcd_name(type),
                             c, pos);
            }
        }
    }
}

int main(void) {
    test_decoder_is_consistent();
    test_digits_and_letters();
    test_locations();
    test_fallback();
    test_shows();
    test_indicators();
    test_no_crosstalk();
    return check_summary("test_display");
}

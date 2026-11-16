// pet_face (watch-faces/complication/pet_face.c) on the face harness, on both
// LCD types: what the top line shows for an egg, a baby, an adult, a
// sleeping pet, a dead one and an unreadable save, and that going from one
// to the next leaves nothing of the old picture. The rules of the pet and
// the choice of sprite are tested in test_pet.c.

#include <stdio.h>
#include <string.h>

#include "check.h"
#include "harness.h"
#include "segment_art.h"
#include "watch_common_display.h"
#include "watch_utility.h"
#include "complication/pet_face.h"

#define A SEGMENT_ART_A
#define B SEGMENT_ART_B
#define C SEGMENT_ART_C
#define D SEGMENT_ART_D
#define E SEGMENT_ART_E
#define F SEGMENT_ART_F
#define G SEGMENT_ART_G
#define H SEGMENT_ART_H

static watch_lcd_type_t lcd;

static watch_date_time_t noon(void) {
    watch_date_time_t dt = {0};
    dt.unit.year = 2026 - WATCH_RTC_REFERENCE_YEAR;
    dt.unit.month = 3;
    dt.unit.day = 1;
    dt.unit.hour = 12;
    return dt;
}

// Boots the face with `saved` as its save file (NULL: no file). The
// inactivity timeout is 30 minutes, so the face stays on screen while a few
// minutes pass.
static void start_face(const pet_state_t *saved) {
    movement_settings_t settings = harness_default_settings();
    settings.bit.to_interval = 3;
    harness_init(lcd, noon(), settings);
    if (saved != NULL) {
        uint8_t buf[PET_SAVE_SIZE];
        pet_serialize(saved, buf);
        harness_write_file("pet.bin", buf, sizeof(buf));
    }
    harness_load_face(&pet_face);
}

// A pet of the given stage, last updated at the time the harness starts at,
// content but not at full energy, so it can be put to bed.
static pet_state_t pet_at(pet_stage_t stage) {
    pet_state_t pet;
    pet_init(&pet, watch_utility_date_time_to_unix_time(noon(), 0));
    pet.stage = stage;
    pet.fullness = pet.happiness = PET_STAT_MAX;
    pet.energy = 50;
    pet.age_minutes = stage == PET_STAGE_BABY ? 60 : 4 * 24 * 60;
    return pet;
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

// The top line: on custom the sprite, as the segments of the three top-left
// characters with the two top-right ones blank; on classic two letters.
static void check_top(uint8_t left, uint8_t middle, uint8_t right, const char *classic, const char *what) {
    int failures = check_failures;
    if (lcd == WATCH_LCD_TYPE_CUSTOM) {
        // Positions 0, 1 and 10 are the top-left group, 2 and 3 the top right.
        CHECK_EQ_INT(segments_at(0), left, "left");
        CHECK_EQ_INT(segments_at(1), middle, "middle");
        CHECK_EQ_INT(segments_at(10), right, "right");
        CHECK_EQ_INT(segments_at(2), 0, "4th");
        CHECK_EQ_INT(segments_at(3), 0, "5th");
    } else {
        CHECK(fake_lcd_shows(WATCH_POSITION_TOP, classic));
    }
    if (check_failures != failures) {
        fprintf(stderr, "(the failures above: %s, %s LCD)\n", what, lcd == WATCH_LCD_TYPE_CUSTOM ? "custom" : "classic");
    }
}

#define EGG A | B | C | D | E | F
#define ADULT_L (A | D | E | F)
#define ADULT_R (A | B | C | D)

static void test_egg_hatches(void) {
    start_face(NULL);
    check_top(0, EGG, 0, "O ", "egg");
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, " FEEd "));
    CHECK(!harness_indicator(WATCH_INDICATOR_BELL));

    harness_advance(PET_EGG_MINUTES * 60 + 1);
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "still on the pet");
    check_top(C, B | D | F, E, "HA", "happy baby");
}

static void test_adult_goes_to_bed_and_gets_up(void) {
    pet_state_t pet = pet_at(PET_STAGE_ADULT);
    start_face(&pet);
    check_top(ADULT_L, B | F | G, ADULT_R, "CO", "adult, not quite content");

    harness_press(BUTTON_LIGHT, PRESS_SHORT);  // PLAY
    harness_press(BUTTON_LIGHT, PRESS_SHORT);  // bEd
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    check_top(ADULT_L, G, ADULT_R, "SL", "asleep");
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, "GEt UP"));
    // Classic has no sleep indicator.
    if (lcd == WATCH_LCD_TYPE_CUSTOM) CHECK(harness_indicator(WATCH_INDICATOR_SLEEP));

    harness_press(BUTTON_ALARM, PRESS_SHORT);
    check_top(ADULT_L, B | F | G, ADULT_R, "CO", "awake again");
    CHECK(!harness_indicator(WATCH_INDICATOR_SLEEP));
}

static void test_moods_of_a_baby(void) {
    pet_state_t pet = pet_at(PET_STAGE_BABY);
    pet.fullness = 10;
    start_face(&pet);
    check_top(C, C | D | E | G, E, "HU", "hungry baby");
    CHECK(harness_indicator(WATCH_INDICATOR_BELL));

    harness_press(BUTTON_ALARM, PRESS_SHORT);  // FEEd: 10 -> 40
    check_top(C, B | F | G, E, "CO", "fed baby");
    CHECK(!harness_indicator(WATCH_INDICATOR_BELL));

    pet = pet_at(PET_STAGE_BABY);
    pet.happiness = 10;
    start_face(&pet);
    check_top(C, C | E | G, E, "SA", "sad baby");

    pet = pet_at(PET_STAGE_BABY);
    pet.energy = 10;
    start_face(&pet);
    check_top(C, D | G, E, "tI", "tired baby");
}

static void test_grave_then_new_egg(void) {
    pet_state_t pet = pet_at(PET_STAGE_DEAD);
    start_face(&pet);
    check_top(D, D | G | H, D, "RI", "dead");
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, "AGE  4"));

    harness_press(BUTTON_ALARM, PRESS_LONG);
    check_top(0, EGG, 0, "O ", "new egg");
}

// A save that can't be read shows text, not a pet, and a long ALARM replaces
// it with an egg: the text's four letters must all go.
static void test_unreadable_save_then_new_egg(void) {
    movement_settings_t settings = harness_default_settings();
    harness_init(lcd, noon(), settings);
    harness_write_file("pet.bin", "?", 1);
    harness_load_face(&pet_face);
    CHECK(fake_lcd_shows(WATCH_POSITION_TOP, lcd == WATCH_LCD_TYPE_CUSTOM ? "FILE " : "FI"));
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, "Error "));

    harness_press(BUTTON_ALARM, PRESS_LONG);
    check_top(0, EGG, 0, "O ", "egg after an unreadable save");

    uint8_t buf[PET_SAVE_SIZE + 1];
    CHECK_EQ_INT(harness_read_file("pet.bin", buf, sizeof(buf)), PET_SAVE_SIZE, "the egg was saved");
}

int main(void) {
    const watch_lcd_type_t types[] = {WATCH_LCD_TYPE_CLASSIC, WATCH_LCD_TYPE_CUSTOM};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        lcd = types[i];
        test_egg_hatches();
        test_adult_goes_to_bed_and_gets_up();
        test_moods_of_a_baby();
        test_grave_then_new_egg();
        test_unreadable_save_then_new_egg();
    }
    return check_summary("test_face_pet");
}

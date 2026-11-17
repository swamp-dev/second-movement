// Captures of pet_face: the pet at each life stage and mood, both frames of
// each picture, asleep, in low-energy mode, dead, the unreadable-save screen,
// and every text the bottom line shows.

#include <stdio.h>

#include "harness.h"
#include "shots.h"
#include "watch_utility.h"
#include "complication/pet_face.h"

static watch_lcd_type_t lcd;

static watch_date_time_t noon(void) {
    watch_date_time_t dt = {0};
    dt.unit.year = 2026 - WATCH_RTC_REFERENCE_YEAR;
    dt.unit.month = 3;
    dt.unit.day = 1;
    dt.unit.hour = 12;
    return dt;
}

// Boots the face with `saved` as its save file (NULL: no file). Low-energy
// mode starts after 10 minutes, and the face stays on screen until then.
static void start_face(const pet_state_t *saved) {
    movement_settings_t settings = harness_default_settings();
    settings.bit.to_interval = 3;
    settings.bit.le_interval = 1;
    harness_init(lcd, noon(), settings);
    if (saved != NULL) {
        uint8_t buf[PET_SAVE_SIZE];
        pet_serialize(saved, buf);
        harness_write_file("pet.bin", buf, sizeof(buf));
    }
    harness_load_face(&pet_face);
}

// A content pet of the given stage, last updated when the harness starts.
static pet_state_t pet_at(pet_stage_t stage) {
    pet_state_t pet;
    pet_init(&pet, watch_utility_date_time_to_unix_time(noon(), 0));
    pet.stage = stage;
    pet.fullness = pet.happiness = PET_STAT_MAX;
    pet.energy = 50;
    pet.age_minutes = stage == PET_STAGE_BABY ? 60 : 4 * 24 * 60;
    return pet;
}

// The screen as the face opens, then one tick later: the picture's two frames.
static void both_frames(const char *name) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s-frame1", name);
    shot(buf);
    harness_tick(1);
    snprintf(buf, sizeof(buf), "%s-frame2", name);
    shot(buf);
}

int main(void) {
    for (size_t l = 0; l < SHOT_NUM_LCDS; l++) {
        lcd = SHOT_LCDS[l];
        pet_state_t pet;

        start_face(NULL);
        both_frames("01-egg");

        pet = pet_at(PET_STAGE_BABY);
        start_face(&pet);
        both_frames("02-baby-content");

        pet = pet_at(PET_STAGE_BABY);
        pet.fullness = 10;
        start_face(&pet);
        shot("03-baby-hungry");

        pet = pet_at(PET_STAGE_BABY);
        pet.happiness = 10;
        start_face(&pet);
        shot("04-baby-sad");

        pet = pet_at(PET_STAGE_BABY);
        pet.energy = 10;
        start_face(&pet);
        shot("05-baby-tired");

        pet = pet_at(PET_STAGE_BABY);
        pet.asleep = true;
        start_face(&pet);
        both_frames("06-baby-asleep");

        pet = pet_at(PET_STAGE_ADULT);
        start_face(&pet);
        both_frames("07-adult-content");

        pet = pet_at(PET_STAGE_ADULT);
        pet.asleep = true;
        start_face(&pet);
        both_frames("08-adult-asleep");

        pet = pet_at(PET_STAGE_ADULT);
        start_face(&pet);
        harness_advance(10 * 60 + 1);
        shot("09-adult-low-energy-mode");

        pet = pet_at(PET_STAGE_DEAD);
        start_face(&pet);
        shot("10-dead");

        harness_init(lcd, noon(), harness_default_settings());
        harness_write_file("pet.bin", "?", 1);
        harness_load_face(&pet_face);
        shot("11-unreadable-save");

        // The bottom line: LIGHT steps through the actions, ALARM performs one.
        pet = pet_at(PET_STAGE_ADULT);
        start_face(&pet);
        harness_press(BUTTON_ALARM, PRESS_SHORT);  // a full pet won't eat
        shot("12-action-refused");
        harness_press(BUTTON_LIGHT, PRESS_SHORT);
        shot("13-action-play");
        harness_press(BUTTON_LIGHT, PRESS_SHORT);
        shot("14-action-bed");
        harness_press(BUTTON_ALARM, PRESS_SHORT);
        shot("15-action-get-up");
        harness_press(BUTTON_ALARM, PRESS_SHORT);
        harness_press(BUTTON_LIGHT, PRESS_SHORT);
        shot("16-action-status");
        harness_press(BUTTON_ALARM, PRESS_SHORT);
        shot("17-status-fullness");
        harness_press(BUTTON_ALARM, PRESS_SHORT);
        shot("18-status-happiness");
        harness_press(BUTTON_ALARM, PRESS_SHORT);
        shot("19-status-energy");
        harness_press(BUTTON_ALARM, PRESS_SHORT);
        shot("20-status-age");
    }
    return 0;
}

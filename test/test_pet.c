// Host tests for lib/pet: decay over time, actions, the save format and the
// screen text.

#include <string.h>
#include "check.h"
#include "pet.h"

#define HOUR 3600UL
#define T0 1800000000UL

static pet_state_t baby(void) {
    pet_state_t pet;
    pet_init(&pet, T0);
    pet_advance(&pet, PET_EGG_MINUTES * 60);
    return pet;
}

static bool same(const pet_state_t *a, const pet_state_t *b) {
    return a->last_update == b->last_update && a->age_minutes == b->age_minutes &&
           a->critical_minutes == b->critical_minutes && a->stage == b->stage &&
           a->fullness == b->fullness && a->happiness == b->happiness &&
           a->energy == b->energy && a->remainder_seconds == b->remainder_seconds &&
           a->asleep == b->asleep;
}

// ---- pet_advance ----

static void test_egg_hatches_with_full_stats(void) {
    pet_state_t pet;
    pet_init(&pet, T0);
    CHECK_EQ_INT(pet.stage, PET_STAGE_EGG, "new pet");
    CHECK_EQ_INT(pet.last_update, T0, "new pet");
    CHECK(!pet_needs_attention(&pet));

    CHECK(!pet_advance(&pet, 59));
    CHECK(pet_advance(&pet, 1));
    CHECK_EQ_INT(pet.age_minutes, 1, "after 60 s");

    pet_advance(&pet, PET_EGG_MINUTES * 60 - 61);
    CHECK_EQ_INT(pet.stage, PET_STAGE_EGG, "one second before hatching");
    pet_advance(&pet, 1);
    CHECK_EQ_INT(pet.stage, PET_STAGE_BABY, "at hatching time");
    CHECK_EQ_INT(pet.fullness, PET_STAT_MAX, "hatchling");
    CHECK_EQ_INT(pet.happiness, PET_STAT_MAX, "hatchling");
    CHECK_EQ_INT(pet.energy, PET_STAT_MAX, "hatchling");
}

static void test_ignored_pet_needs_attention_after_eight_waking_hours(void) {
    pet_state_t pet = baby();
    pet_advance(&pet, 7 * HOUR);
    CHECK(!pet_needs_attention(&pet));
    pet_advance(&pet, 1 * HOUR);
    CHECK(pet_needs_attention(&pet));
    CHECK(pet.fullness <= PET_STAT_CRITICAL);
    CHECK(pet.happiness > PET_STAT_CRITICAL);
    CHECK(pet.energy > PET_STAT_CRITICAL);
}

static void test_stats_stop_at_zero(void) {
    pet_state_t pet = baby();
    pet_advance(&pet, 12 * HOUR);
    pet_advance(&pet, 8 * HOUR);
    CHECK_EQ_INT(pet.fullness, 0, "after 20 h");
    CHECK_EQ_INT(pet.happiness, 0, "after 20 h");
    CHECK_EQ_INT(pet.energy, 0, "after 20 h");
    CHECK_EQ_INT(pet.stage, PET_STAGE_BABY, "not dead yet");
}

// The ticket's first "done when": the result depends on the time that passed,
// not on how it was sliced into updates (every second with the face open,
// every minute in low-energy mode, or once after hours on another face).
static void test_advance_is_the_same_however_time_is_sliced(void) {
    const uint32_t total = 11 * HOUR + 1234;
    pet_state_t once = baby(), seconds = baby(), minutes = baby(), odd = baby();
    pet_apply_action(&odd, PET_ACTION_LIGHTS); // not at full energy yet: refused
    pet_advance(&once, total);
    for (uint32_t t = 0; t < total; t++) pet_advance(&seconds, 1);
    for (uint32_t t = 0; t + 60 <= total; t += 60) pet_advance(&minutes, 60);
    pet_advance(&minutes, total % 60);
    uint32_t left = total;
    for (uint32_t chunk = 1; left > 0; chunk = chunk * 7 % 5003 + 1) {
        uint32_t step = chunk < left ? chunk : left;
        pet_advance(&odd, step);
        left -= step;
    }
    CHECK(same(&once, &seconds));
    CHECK(same(&once, &minutes));
    CHECK(same(&once, &odd));
}

static void test_slicing_holds_across_sleep_and_waking(void) {
    pet_state_t a = baby();
    pet_advance(&a, 6 * HOUR);
    CHECK(pet_apply_action(&a, PET_ACTION_LIGHTS));
    pet_state_t b = a;
    pet_advance(&a, 9 * HOUR); // long enough to wake up again inside the span
    for (uint32_t t = 0; t < 9 * HOUR; t += 7) pet_advance(&b, t + 7 <= 9 * HOUR ? 7 : 9 * HOUR - t);
    CHECK(!a.asleep);
    CHECK(same(&a, &b));
}

static void test_sleep_restores_energy_and_pet_wakes_when_rested(void) {
    pet_state_t pet = baby();
    pet_advance(&pet, 10 * HOUR);
    const uint8_t tired = pet.energy, fullness = pet.fullness;
    CHECK(pet_apply_action(&pet, PET_ACTION_LIGHTS));
    pet_advance(&pet, HOUR);
    CHECK(pet.asleep);
    CHECK_EQ_INT(pet.energy, tired + 60 / PET_ASLEEP_ENERGY_PERIOD, "an hour asleep");
    CHECK(fullness - pet.fullness <= 60 / PET_ASLEEP_FULLNESS_PERIOD + 1);
    pet_advance(&pet, 5 * HOUR);
    CHECK(!pet.asleep);
    CHECK(pet.energy > PET_STAT_MAX - 30);
}

static void test_low_energy_asleep_is_not_an_emergency(void) {
    pet_state_t pet = baby();
    pet.energy = 5;
    CHECK(pet_needs_attention(&pet));
    pet.asleep = true;
    CHECK(!pet_needs_attention(&pet));
    pet.fullness = PET_STAT_CRITICAL;
    CHECK(pet_needs_attention(&pet));
}

static void test_baby_grows_up(void) {
    pet_state_t pet = baby();
    while (pet.age_minutes < PET_ADULT_AGE_MINUTES - 1) {
        pet_advance(&pet, 60);
        pet.fullness = pet.happiness = pet.energy = PET_STAT_MAX;
    }
    CHECK_EQ_INT(pet.stage, PET_STAGE_BABY, "a minute before growing up");
    pet_advance(&pet, 60);
    CHECK_EQ_INT(pet.stage, PET_STAGE_ADULT, "at adult age");
}

static void test_dies_after_a_day_of_neglect_and_stays_dead(void) {
    pet_state_t pet = baby();
    pet.fullness = PET_STAT_CRITICAL + 1;
    pet_advance(&pet, PET_AWAKE_FULLNESS_PERIOD * 60); // now critical
    CHECK(pet_needs_attention(&pet));
    uint32_t waited = 0;
    while (pet.stage != PET_STAGE_DEAD && waited < 48 * HOUR) {
        pet_advance(&pet, 60);
        waited += 60;
    }
    CHECK_EQ_INT(pet.stage, PET_STAGE_DEAD, "after neglect");
    CHECK(waited >= 23 * HOUR && waited <= 24 * HOUR);
    CHECK(!pet_needs_attention(&pet));
    CHECK(!pet.asleep);

    const pet_state_t dead = pet;
    CHECK(pet_advance(&pet, 5 * HOUR));
    pet.last_update = dead.last_update;
    CHECK(same(&pet, &dead));
}

static void test_care_resets_the_death_clock(void) {
    pet_state_t pet = baby();
    pet.fullness = 0;
    pet_advance(&pet, 12 * HOUR);
    pet_advance(&pet, 11 * HOUR);
    CHECK_EQ_INT(pet.stage, PET_STAGE_BABY, "23 h critical");
    pet.fullness = pet.happiness = pet.energy = PET_STAT_MAX;
    pet_advance(&pet, 60);
    CHECK_EQ_INT(pet.critical_minutes, 0, "after care");
    pet_advance(&pet, 12 * HOUR);
    CHECK_EQ_INT(pet.stage, PET_STAGE_BABY, "long after care");
}

static void test_one_catch_up_is_capped_and_cannot_kill_a_healthy_pet(void) {
    pet_state_t capped = baby(), huge = baby();
    pet_advance(&capped, PET_MAX_CATCHUP_SECONDS);
    pet_advance(&huge, 400UL * 24 * HOUR);
    CHECK(same(&capped, &huge));
    CHECK(huge.stage != PET_STAGE_DEAD);

    pet_state_t max = baby();
    pet_advance(&max, UINT32_MAX);
    CHECK(same(&capped, &max));
}

// ---- pet_advance_to ----

static void test_advance_to_uses_the_clock(void) {
    pet_state_t a = baby(), b = baby();
    CHECK(pet_advance_to(&a, T0 + 3 * HOUR));
    pet_advance(&b, 3 * HOUR);
    CHECK_EQ_INT(a.last_update, T0 + 3 * HOUR, "moved to now");
    b.last_update = a.last_update;
    CHECK(same(&a, &b));
}

static void test_clock_set_backward_counts_as_no_time(void) {
    pet_state_t pet = baby();
    const pet_state_t before = pet;
    CHECK(!pet_advance_to(&pet, T0 - 30 * 24 * HOUR));
    CHECK_EQ_INT(pet.last_update, T0 - 30 * 24 * HOUR, "re-anchored");
    pet.last_update = before.last_update;
    CHECK(same(&pet, &before));
    // ...and time counts normally from the new anchor.
    pet.last_update = 1000;
    pet_advance_to(&pet, 1000 + 2 * HOUR);
    CHECK_EQ_INT(pet.fullness, before.fullness - 120 / PET_AWAKE_FULLNESS_PERIOD, "2 h on");
}

static void test_clock_set_far_forward_is_capped(void) {
    pet_state_t jumped = baby(), capped = baby();
    pet_advance_to(&jumped, T0 + 10UL * 365 * 24 * HOUR);
    pet_advance(&capped, PET_MAX_CATCHUP_SECONDS);
    capped.last_update = jumped.last_update;
    CHECK(same(&jumped, &capped));
    CHECK(jumped.stage != PET_STAGE_DEAD);
}

// ---- pet_apply_action ----

static void test_feed(void) {
    pet_state_t pet = baby();
    CHECK(!pet_apply_action(&pet, PET_ACTION_FEED)); // full
    pet.fullness = 50;
    CHECK(pet_apply_action(&pet, PET_ACTION_FEED));
    CHECK_EQ_INT(pet.fullness, 50 + PET_FEED_FULLNESS, "fed");
    CHECK(pet_apply_action(&pet, PET_ACTION_FEED));
    CHECK_EQ_INT(pet.fullness, PET_STAT_MAX, "fed to the brim");
}

static void test_play(void) {
    pet_state_t pet = baby();
    CHECK(!pet_apply_action(&pet, PET_ACTION_PLAY)); // already delighted
    pet.happiness = 40;
    pet.energy = 50;
    CHECK(pet_apply_action(&pet, PET_ACTION_PLAY));
    CHECK_EQ_INT(pet.happiness, 40 + PET_PLAY_HAPPINESS, "played");
    CHECK_EQ_INT(pet.energy, 50 - PET_PLAY_ENERGY_COST, "played");
    pet.happiness = 90;
    CHECK(pet_apply_action(&pet, PET_ACTION_PLAY));
    CHECK_EQ_INT(pet.happiness, PET_STAT_MAX, "capped");

    pet.happiness = 10;
    pet.energy = PET_STAT_CRITICAL;
    const pet_state_t tired = pet;
    CHECK(!pet_apply_action(&pet, PET_ACTION_PLAY)); // too tired
    CHECK(same(&pet, &tired));
}

static void test_lights(void) {
    pet_state_t pet = baby();
    CHECK(!pet_apply_action(&pet, PET_ACTION_LIGHTS)); // wide awake
    pet.energy = 60;
    pet.fullness = 50;
    pet.happiness = 50;
    CHECK(pet_apply_action(&pet, PET_ACTION_LIGHTS));
    CHECK(pet.asleep);
    const pet_state_t asleep = pet;
    CHECK(!pet_apply_action(&pet, PET_ACTION_FEED));
    CHECK(!pet_apply_action(&pet, PET_ACTION_PLAY));
    CHECK(same(&pet, &asleep));
    CHECK(pet_apply_action(&pet, PET_ACTION_LIGHTS));
    CHECK(!pet.asleep);
}

static void test_egg_and_dead_pet_only_take_status(void) {
    pet_state_t pets[2];
    pet_init(&pets[0], T0);
    pets[1] = baby();
    pets[1].stage = PET_STAGE_DEAD;
    pets[1].fullness = pets[1].happiness = pets[1].energy = 10;
    for (int i = 0; i < 2; i++) {
        const pet_state_t before = pets[i];
        CHECK(!pet_apply_action(&pets[i], PET_ACTION_FEED));
        CHECK(!pet_apply_action(&pets[i], PET_ACTION_PLAY));
        CHECK(!pet_apply_action(&pets[i], PET_ACTION_LIGHTS));
        CHECK(pet_apply_action(&pets[i], PET_ACTION_STATUS));
        CHECK(!pet_apply_action(&pets[i], PET_ACTION_COUNT));
        CHECK(same(&pets[i], &before));
    }
}

// ---- pet_page_after_action ----

static void test_page_after_action(void) {
    CHECK_EQ_INT(pet_page_after_action(PET_ACTION_FEED, true, PET_PAGE_ACTION), PET_PAGE_FULLNESS, "fed");
    CHECK_EQ_INT(pet_page_after_action(PET_ACTION_FEED, true, PET_PAGE_FULLNESS), PET_PAGE_FULLNESS, "fed again");
    CHECK_EQ_INT(pet_page_after_action(PET_ACTION_PLAY, true, PET_PAGE_ACTION), PET_PAGE_HAPPINESS, "played");
    CHECK_EQ_INT(pet_page_after_action(PET_ACTION_LIGHTS, true, PET_PAGE_REFUSED), PET_PAGE_ACTION, "lights");
    CHECK_EQ_INT(pet_page_after_action(PET_ACTION_FEED, false, PET_PAGE_ACTION), PET_PAGE_REFUSED, "refused");
    CHECK_EQ_INT(pet_page_after_action(PET_ACTION_LIGHTS, false, PET_PAGE_ACTION), PET_PAGE_REFUSED, "refused");

    pet_page_t page = PET_PAGE_ACTION;
    const pet_page_t cycle[] = {PET_PAGE_FULLNESS, PET_PAGE_HAPPINESS, PET_PAGE_ENERGY, PET_PAGE_AGE, PET_PAGE_FULLNESS};
    for (size_t i = 0; i < sizeof(cycle) / sizeof(cycle[0]); i++) {
        page = pet_page_after_action(PET_ACTION_STATUS, true, page);
        CHECK_EQ_INT(page, cycle[i], "status press %u", (unsigned)i);
    }
}

// ---- pet_render ----

// Every screen must fit both LCDs.
static void check_screen_fits(const pet_screen_t *s) {
    CHECK(strlen(s->top) == 5); // always all 5, so a shorter mood leaves nothing behind
    CHECK(strlen(s->top_fallback) == 2);
    CHECK(strlen(s->bottom) == 6);
    // The classic LCD ties the top and bottom segments of bottom characters 0
    // and 2 together, so only these draw there (the display code swaps in
    // look-alikes for A, L, t and U).
    CHECK(strchr(" ACEGHLOSUnort0123456789", s->bottom[0]) != NULL);
    CHECK(strchr(" ACEGHLOSUnort0123456789", s->bottom[2]) != NULL);
    // ...and its second top character has too few segments for most letters.
    CHECK(strchr(" AEHILORU", s->top_fallback[1]) != NULL);
}

static void check_screen(const pet_state_t *pet, pet_action_t action, pet_page_t page,
                         const char *top, const char *fallback, const char *bottom, int line) {
    pet_screen_t s;
    memset(&s, 0x7f, sizeof(s));
    pet_render(pet, action, page, &s);
    check_count++;
    if (strcmp(s.top, top) || strcmp(s.top_fallback, fallback) || strcmp(s.bottom, bottom)) {
        check_failures++;
        fprintf(stderr, "%s:%d: screen [%s|%s|%s], expected [%s|%s|%s]\n", __FILE__, line,
                s.top, s.top_fallback, s.bottom, top, fallback, bottom);
    }
    check_screen_fits(&s);
}
#define CHECK_SCREEN(pet, action, page, top, fallback, bottom) \
    check_screen(pet, action, page, top, fallback, bottom, __LINE__)

static void test_render_moods(void) {
    pet_state_t pet;
    pet_init(&pet, T0);
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "EGG  ", "O ", " FEEd ");

    pet = baby();
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "HAPPY", "HA", " FEEd ");
    pet.happiness = PET_STAT_CONTENT - 1;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "COOL ", "CO", " FEEd ");
    pet.happiness = PET_STAT_CRITICAL;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "SAd  ", "SA", " FEEd ");
    pet.energy = PET_STAT_CRITICAL;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "tIrEd", "tI", " FEEd ");
    pet.fullness = PET_STAT_CRITICAL;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "HUNGr", "HU", " FEEd ");
    pet.asleep = true;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "SLEEP", "SL", " FEEd ");
    pet.stage = PET_STAGE_DEAD;
    pet.asleep = false;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "RIP  ", "RI", "AGE  0");
}

static void test_render_bottom_line(void) {
    pet_state_t pet = baby();
    pet.fullness = 100;
    pet.happiness = 7;
    pet.energy = 42;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "SAd  ", "SA", " FEEd ");
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_ACTION, "SAd  ", "SA", " PLAY ");
    CHECK_SCREEN(&pet, PET_ACTION_LIGHTS, PET_PAGE_ACTION, "SAd  ", "SA", " bEd  ");
    pet.asleep = true; // the lights action names what it would do now
    CHECK_SCREEN(&pet, PET_ACTION_LIGHTS, PET_PAGE_ACTION, "SLEEP", "SL", "GEt UP");
    pet.asleep = false;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_ACTION, "SAd  ", "SA", "StAtUS");
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_FULLNESS, "SAd  ", "SA", "EA 100");
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_HAPPINESS, "SAd  ", "SA", "HA   7");
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_ENERGY, "SAd  ", "SA", "En  42");
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_REFUSED, "SAd  ", "SA", "  no  ");

    pet.age_minutes = 2 * 24 * 60 + 500;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "SAd  ", "SA", "CH   2");
    pet.stage = PET_STAGE_ADULT;
    pet.age_minutes = 45 * 24 * 60;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "SAd  ", "SA", "Ad  45");
    pet.age_minutes = UINT32_MAX;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "SAd  ", "SA", "Ad 999");
    pet.stage = PET_STAGE_DEAD;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_FULLNESS, "RIP  ", "RI", "AGE999");

    pet_init(&pet, T0);
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "EGG  ", "O ", "EGG  0");
    // An out-of-range action or page still yields well-formed strings.
    CHECK_SCREEN(&pet, PET_ACTION_COUNT, PET_PAGE_ACTION, "EGG  ", "O ", "      ");
    CHECK_SCREEN(&pet, PET_ACTION_FEED, (pet_page_t)99, "EGG  ", "O ", "      ");
}

static void test_render_doze_and_indicators(void) {
    pet_state_t pet = baby();
    pet_screen_t s;
    pet_render(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, &s);
    CHECK(!s.bell && !s.sleep);
    // In low-energy mode the watch shows a sleeping pet whatever its mood...
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_DOZE, "SLEEP", "SL", "      ");
    pet.fullness = 3;
    pet_render(&pet, PET_ACTION_FEED, PET_PAGE_DOZE, &s);
    CHECK(s.bell && !s.sleep); // ...but the bell still tells the truth.
    pet.asleep = true;
    pet_render(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, &s);
    CHECK(s.bell && s.sleep);
    pet.stage = PET_STAGE_DEAD;
    pet.asleep = false;
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_DOZE, "RIP  ", "RI", "      ");
    pet_render(&pet, PET_ACTION_FEED, PET_PAGE_DOZE, &s);
    CHECK(!s.bell && !s.sleep);
}

// ---- save file ----

static void test_save_round_trip(void) {
    pet_state_t pet = baby();
    pet_advance_to(&pet, T0 + 5 * HOUR + 17);
    pet_apply_action(&pet, PET_ACTION_LIGHTS);
    pet.critical_minutes = 0x1234;
    pet.age_minutes = 0x00abcdef;

    uint8_t buf[PET_SAVE_SIZE];
    pet_serialize(&pet, buf);
    CHECK_EQ_INT(buf[0], PET_SAVE_VERSION, "version byte comes first");

    pet_state_t loaded;
    memset(&loaded, 0xee, sizeof(loaded));
    CHECK(pet_deserialize(buf, sizeof(buf), &loaded));
    CHECK(same(&pet, &loaded));

    // Extra bytes after a version-1 record are ignored.
    uint8_t longer[PET_SAVE_SIZE + 4] = {0};
    memcpy(longer, buf, sizeof(buf));
    CHECK(pet_deserialize(longer, sizeof(longer), &loaded));
    CHECK(same(&pet, &loaded));
}

static void test_save_layout_is_fixed(void) {
    // Version 1 bytes, as an older firmware wrote them: this must keep loading.
    const uint8_t v1[PET_SAVE_SIZE] = {
        1, PET_STAGE_ADULT, 1, 80, 55, 30, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0xdd, 0xcc, 0xbb, 0x6a, 59,
    };
    pet_state_t pet;
    CHECK(pet_deserialize(v1, sizeof(v1), &pet));
    CHECK_EQ_INT(pet.stage, PET_STAGE_ADULT, "v1");
    CHECK(pet.asleep);
    CHECK_EQ_INT(pet.fullness, 80, "v1");
    CHECK_EQ_INT(pet.happiness, 55, "v1");
    CHECK_EQ_INT(pet.energy, 30, "v1");
    CHECK_EQ_INT(pet.critical_minutes, 0x1234, "v1");
    CHECK_EQ_INT(pet.age_minutes, 0x12345678, "v1");
    CHECK_EQ_INT(pet.last_update, 0x6abbccddUL, "v1");
    CHECK_EQ_INT(pet.remainder_seconds, 59, "v1");

    uint8_t out[PET_SAVE_SIZE];
    pet_serialize(&pet, out);
    CHECK(memcmp(out, v1, sizeof(v1)) == 0);
}

static void test_unreadable_saves_are_rejected(void) {
    pet_state_t good = baby();
    uint8_t buf[PET_SAVE_SIZE];
    pet_serialize(&good, buf);

    pet_state_t pet;
    memset(&pet, 0x55, sizeof(pet));
    const pet_state_t untouched = pet;

    CHECK(!pet_deserialize(buf, 0, &pet));                 // missing or empty file
    CHECK(!pet_deserialize(buf, PET_SAVE_SIZE - 1, &pet)); // truncated

    const struct { int offset; uint8_t value; } bad[] = {
        {0, 0}, {0, PET_SAVE_VERSION + 1}, {0, 0xff}, // unknown version
        {1, PET_STAGE_DEAD + 1},                      // no such stage
        {2, 2},                                       // not a bool
        {3, PET_STAT_MAX + 1}, {4, 0xff}, {5, 101},   // stats out of range
        {16, 60},                                     // not a remainder
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        uint8_t corrupt[PET_SAVE_SIZE];
        memcpy(corrupt, buf, sizeof(buf));
        corrupt[bad[i].offset] = bad[i].value;
        CHECK_EQ_INT(pet_deserialize(corrupt, sizeof(corrupt), &pet), false, "byte %d = %u", bad[i].offset, bad[i].value);
    }
    uint8_t zeros[PET_SAVE_SIZE] = {0}; // what filesystem_read_file leaves on failure
    CHECK(!pet_deserialize(zeros, sizeof(zeros), &pet));
    CHECK(memcmp(&pet, &untouched, sizeof(pet)) == 0);
}

// "State survives a reset": a pet restored from its save file and caught up
// to the clock is the pet that never stopped running, as long as the save is
// younger than the catch-up cap. The face saves often enough for that.
// ---- pet_load ----

static void test_load_no_file_means_a_new_egg(void) {
    pet_state_t pet = baby(), before = pet;
    uint8_t buf[PET_SAVE_SIZE] = {0};
    CHECK_EQ_INT(pet_load(-1, false, buf, &pet), PET_LOAD_NO_SAVE, "no file");
    CHECK(same(&pet, &before));
}

static void test_load_good_file(void) {
    pet_state_t saved = baby(), pet;
    uint8_t buf[PET_SAVE_SIZE];
    saved.fullness = 42;
    pet_serialize(&saved, buf);
    memset(&pet, 0, sizeof(pet));
    CHECK_EQ_INT(pet_load(PET_SAVE_SIZE, true, buf, &pet), PET_LOAD_OK, "good file");
    CHECK(same(&pet, &saved));
}

// The point of #39: a file that exists is never treated as "no pet yet",
// whatever is wrong with it, so nothing overwrites it without the user asking.
static void test_load_existing_file_that_cannot_be_used_is_unreadable(void) {
    pet_state_t good = baby(), pet = good, before = pet;
    uint8_t buf[PET_SAVE_SIZE];
    pet_serialize(&good, buf);

    // The read itself failed (flash error), though the bytes would be fine.
    CHECK_EQ_INT(pet_load(PET_SAVE_SIZE, false, buf, &pet), PET_LOAD_UNREADABLE, "read failed");
    // Written by a newer firmware.
    buf[0] = PET_SAVE_VERSION + 1;
    CHECK_EQ_INT(pet_load(PET_SAVE_SIZE, true, buf, &pet), PET_LOAD_UNREADABLE, "newer version");
    buf[0] = PET_SAVE_VERSION;
    // Corrupt contents.
    buf[3] = PET_STAT_MAX + 1;
    CHECK_EQ_INT(pet_load(PET_SAVE_SIZE, true, buf, &pet), PET_LOAD_UNREADABLE, "corrupt");
    buf[3] = good.fullness;
    // Truncated, and empty (filesystem_read_file reports an empty file as a failed read).
    CHECK_EQ_INT(pet_load(PET_SAVE_SIZE - 1, true, buf, &pet), PET_LOAD_UNREADABLE, "truncated");
    CHECK_EQ_INT(pet_load(0, false, buf, &pet), PET_LOAD_UNREADABLE, "empty");
    CHECK(same(&pet, &before));
}

static void test_render_unreadable(void) {
    pet_screen_t s;
    memset(&s, 0x7f, sizeof(s));
    pet_render_unreadable(&s);
    CHECK(strcmp(s.top, "FILE ") == 0);
    CHECK(strcmp(s.top_fallback, "FI") == 0);
    CHECK(strcmp(s.bottom, "Error ") == 0);
    CHECK(!s.bell);
    CHECK(!s.sleep);
    check_screen_fits(&s);
}

static void test_reload_then_catch_up_equals_never_resetting(void) {
    CHECK(PET_SAVE_INTERVAL_SECONDS < PET_MAX_CATCHUP_SECONDS);

    pet_state_t running = baby();
    pet_advance_to(&running, T0 + 3 * HOUR + 41);
    pet_apply_action(&running, PET_ACTION_FEED);

    uint8_t file[PET_SAVE_SIZE];
    pet_serialize(&running, file);

    const uint32_t later = T0 + 3 * HOUR + 41 + PET_SAVE_INTERVAL_SECONDS + 1234;
    for (uint32_t t = running.last_update + 60; t <= later; t += 60) pet_advance_to(&running, t);
    pet_advance_to(&running, later);

    pet_state_t reloaded;
    CHECK(pet_deserialize(file, sizeof(file), &reloaded));
    pet_advance_to(&reloaded, later);
    CHECK(same(&running, &reloaded));
}

// Updated every minute (what the face's advise hook does), a neglected pet
// dies on schedule: about 8 h to go hungry plus 24 h critical.
static void test_minute_by_minute_neglect_kills_within_two_days(void) {
    pet_state_t pet = baby();
    uint32_t now = T0;
    while (pet.stage != PET_STAGE_DEAD && now < T0 + 72 * HOUR) pet_advance_to(&pet, now += 60);
    CHECK_EQ_INT(pet.stage, PET_STAGE_DEAD, "after neglect");
    CHECK(now - T0 > 31 * HOUR && now - T0 <= 32 * HOUR);
}

static void test_should_save(void) {
    const pet_state_t saved = baby();
    pet_state_t pet = saved;
    CHECK(!pet_should_save(&saved, &pet));

    pet_advance_to(&pet, T0 + PET_SAVE_INTERVAL_SECONDS - 1);
    CHECK(!pet_should_save(&saved, &pet));
    pet_advance_to(&pet, T0 + PET_SAVE_INTERVAL_SECONDS);
    CHECK(pet_should_save(&saved, &pet));

    pet = saved;
    pet.asleep = true;
    CHECK(pet_should_save(&saved, &pet));
    pet = saved;
    pet.stage = PET_STAGE_ADULT;
    CHECK(pet_should_save(&saved, &pet));

    pet = saved;
    pet_advance_to(&pet, T0 - 1); // clock went backward: keep the new anchor
    CHECK(pet_should_save(&saved, &pet));

    // Nothing about a dead pet changes with time, so once its death is on
    // file it is never written again.
    pet_state_t dead = saved;
    dead.stage = PET_STAGE_DEAD;
    CHECK(pet_should_save(&saved, &dead));
    pet = dead;
    pet_advance_to(&pet, T0 + 10 * PET_SAVE_INTERVAL_SECONDS);
    CHECK(!pet_should_save(&dead, &pet));
    pet_advance_to(&pet, T0 - 1);
    CHECK(!pet_should_save(&dead, &pet));
}

int main(void) {
    test_egg_hatches_with_full_stats();
    test_ignored_pet_needs_attention_after_eight_waking_hours();
    test_stats_stop_at_zero();
    test_advance_is_the_same_however_time_is_sliced();
    test_slicing_holds_across_sleep_and_waking();
    test_sleep_restores_energy_and_pet_wakes_when_rested();
    test_low_energy_asleep_is_not_an_emergency();
    test_baby_grows_up();
    test_dies_after_a_day_of_neglect_and_stays_dead();
    test_care_resets_the_death_clock();
    test_one_catch_up_is_capped_and_cannot_kill_a_healthy_pet();
    test_advance_to_uses_the_clock();
    test_clock_set_backward_counts_as_no_time();
    test_clock_set_far_forward_is_capped();
    test_feed();
    test_play();
    test_lights();
    test_egg_and_dead_pet_only_take_status();
    test_page_after_action();
    test_render_moods();
    test_render_bottom_line();
    test_render_doze_and_indicators();
    test_save_round_trip();
    test_save_layout_is_fixed();
    test_unreadable_saves_are_rejected();
    test_load_no_file_means_a_new_egg();
    test_load_good_file();
    test_load_existing_file_that_cannot_be_used_is_unreadable();
    test_render_unreadable();
    test_reload_then_catch_up_equals_never_resetting();
    test_minute_by_minute_neglect_kills_within_two_days();
    test_should_save();
    return check_summary("test_pet");
}

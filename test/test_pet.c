// Host tests for lib/pet: decay over time, actions, the save format, the
// screen text and the sprite.

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
    // On custom the top is either a sprite or all 5 characters, so a shorter
    // word leaves nothing behind.
    if (!s->has_sprite) CHECK(strlen(s->top) == 5);
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
                         const char *fallback, const char *bottom, int line) {
    pet_screen_t s;
    memset(&s, 0x7f, sizeof(s));
    pet_render(pet, action, page, &s);
    check_count++;
    if (!s.has_sprite || strcmp(s.top_fallback, fallback) || strcmp(s.bottom, bottom)) {
        check_failures++;
        fprintf(stderr, "%s:%d: screen [%s|%s|%s], expected [sprite|%s|%s]\n", __FILE__, line,
                s.has_sprite ? "sprite" : "text", s.top_fallback, s.bottom, fallback, bottom);
    }
    check_screen_fits(&s);
}
#define CHECK_SCREEN(pet, action, page, fallback, bottom) \
    check_screen(pet, action, page, fallback, bottom, __LINE__)

// ---- sprites ----

#define A 0x01 // segment bits as in lib/segment_art: A top, then clockwise
#define B 0x02 // to F, G the middle bar, H the upright in the middle
#define C 0x04
#define D 0x08
#define E 0x10
#define F 0x20
#define G 0x40
#define H 0x80

// The frames around the face: a full box for an adult, the lower half of its
// sides for a baby.
#define ADULT_L (A | D | E | F)
#define ADULT_R (A | B | C | D)
#define BABY_L C
#define BABY_R E

#define FACE_HAPPY (B | F | D)      // eyes and a wide mouth
#define FACE_COOL (B | F | G)       // eyes and a small mouth
#define FACE_HUNGRY (C | D | E | G) // an open mouth
#define FACE_TIRED (D | G)          // eyes shut, mouth shut
#define FACE_SAD (C | E | G)        // a frown
#define FACE_ASLEEP G               // eyes shut

static pet_screen_t rendered(const pet_state_t *pet, pet_page_t page) {
    pet_screen_t s;
    memset(&s, 0x7f, sizeof(s));
    pet_render(pet, PET_ACTION_FEED, page, &s);
    return s;
}

static void check_sprite(const pet_state_t *pet, pet_page_t page, uint8_t left, uint8_t middle, uint8_t right,
                         const char *what) {
    pet_screen_t s = rendered(pet, page);
    CHECK(s.has_sprite);
    CHECK_EQ_INT(s.sprite[0], left, "%s, left", what);
    CHECK_EQ_INT(s.sprite[1], middle, "%s, middle", what);
    CHECK_EQ_INT(s.sprite[2], right, "%s, right", what);
    // The two top-right characters stay clear.
    CHECK_EQ_INT(s.sprite[3], 0, "%s, 4th", what);
    CHECK_EQ_INT(s.sprite[4], 0, "%s, 5th", what);
}

static pet_state_t adult(void) {
    pet_state_t pet = baby();
    pet.stage = PET_STAGE_ADULT;
    pet.age_minutes = PET_ADULT_AGE_MINUTES;
    return pet;
}

// One pet in each mood, in the order pet_render tests for them.
#define NUM_MOODS 6
static pet_state_t in_mood(pet_state_t pet, int mood) {
    switch (mood) {
        case 0: break;                                    // happy
        case 1: pet.happiness = PET_STAT_CONTENT - 1; break; // cool
        case 2: pet.happiness = PET_STAT_CRITICAL; break;
        case 3: pet.energy = PET_STAT_CRITICAL; break;
        case 4: pet.fullness = PET_STAT_CRITICAL; break;
        default: pet.asleep = true; break;
    }
    return pet;
}

static void test_sprite_stages(void) {
    pet_state_t pet;
    pet_init(&pet, T0);
    check_sprite(&pet, PET_PAGE_ACTION, 0, A | B | C | D | E | F, 0, "egg");

    pet = baby();
    check_sprite(&pet, PET_PAGE_ACTION, BABY_L, FACE_HAPPY, BABY_R, "baby");
    pet = adult();
    check_sprite(&pet, PET_PAGE_ACTION, ADULT_L, FACE_HAPPY, ADULT_R, "adult");

    // A cross on the ground.
    pet.stage = PET_STAGE_DEAD;
    check_sprite(&pet, PET_PAGE_ACTION, D, D | G | H, D, "dead");
}

static void test_sprite_moods(void) {
    const uint8_t faces[NUM_MOODS] = {FACE_HAPPY, FACE_COOL, FACE_SAD, FACE_TIRED, FACE_HUNGRY, FACE_ASLEEP};
    const char *const names[NUM_MOODS] = {"happy", "cool", "sad", "tired", "hungry", "asleep"};
    for (int mood = 0; mood < NUM_MOODS; mood++) {
        pet_state_t pet = in_mood(baby(), mood);
        check_sprite(&pet, PET_PAGE_ACTION, BABY_L, faces[mood], BABY_R, names[mood]);
        pet = in_mood(adult(), mood);
        check_sprite(&pet, PET_PAGE_ACTION, ADULT_L, faces[mood], ADULT_R, names[mood]);
    }

    // The worst stat wins, in the order hungry, tired, sad; sleep hides them all.
    pet_state_t pet = adult();
    pet.happiness = pet.energy = pet.fullness = 0;
    check_sprite(&pet, PET_PAGE_ACTION, ADULT_L, FACE_HUNGRY, ADULT_R, "all critical");
    pet.fullness = PET_STAT_MAX;
    check_sprite(&pet, PET_PAGE_ACTION, ADULT_L, FACE_TIRED, ADULT_R, "tired and sad");
    pet.asleep = true;
    check_sprite(&pet, PET_PAGE_ACTION, ADULT_L, FACE_ASLEEP, ADULT_R, "asleep, tired and sad");
}

// In low-energy mode the watch shows a living pet asleep; the egg and the
// grave stay as they are.
static void test_sprite_doze(void) {
    pet_state_t pet = in_mood(adult(), 4);
    check_sprite(&pet, PET_PAGE_DOZE, ADULT_L, FACE_ASLEEP, ADULT_R, "dozing adult");
    pet = baby();
    check_sprite(&pet, PET_PAGE_DOZE, BABY_L, FACE_ASLEEP, BABY_R, "dozing baby");
    pet_init(&pet, T0);
    check_sprite(&pet, PET_PAGE_DOZE, 0, A | B | C | D | E | F, 0, "dozing egg");
    pet = adult();
    pet.stage = PET_STAGE_DEAD;
    check_sprite(&pet, PET_PAGE_DOZE, D, D | G | H, D, "dozing grave");
}

// Every state the player has to tell apart draws differently: the egg, the
// grave, and each mood of the baby and of the adult.
static void test_sprites_differ(void) {
    pet_screen_t all[2 + 2 * NUM_MOODS];
    size_t n = 0;
    pet_state_t pet;
    pet_init(&pet, T0);
    all[n++] = rendered(&pet, PET_PAGE_ACTION);
    pet = adult();
    pet.stage = PET_STAGE_DEAD;
    all[n++] = rendered(&pet, PET_PAGE_ACTION);
    for (int mood = 0; mood < NUM_MOODS; mood++) {
        pet = in_mood(baby(), mood);
        all[n++] = rendered(&pet, PET_PAGE_ACTION);
        pet = in_mood(adult(), mood);
        all[n++] = rendered(&pet, PET_PAGE_ACTION);
    }
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            CHECK_EQ_INT(memcmp(all[i].sprite, all[j].sprite, PET_SPRITE_WIDTH) == 0, 0,
                         "sprites %u and %u", (unsigned)i, (unsigned)j);
        }
    }
}

// The sprite doesn't depend on what the bottom line shows.
static void test_sprite_is_the_same_on_every_page(void) {
    pet_state_t pet = in_mood(adult(), 2);
    pet_screen_t first = rendered(&pet, PET_PAGE_ACTION);
    for (int page = PET_PAGE_FULLNESS; page <= PET_PAGE_REFUSED; page++) {
        pet_screen_t s = rendered(&pet, (pet_page_t)page);
        CHECK_EQ_INT(memcmp(s.sprite, first.sprite, PET_SPRITE_WIDTH), 0, "page %d", page);
    }
}

static void test_render_moods(void) {
    pet_state_t pet;
    pet_init(&pet, T0);
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "O ", " FEEd ");

    pet = baby();
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "HA", " FEEd ");
    pet.happiness = PET_STAT_CONTENT - 1;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "CO", " FEEd ");
    pet.happiness = PET_STAT_CRITICAL;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "SA", " FEEd ");
    pet.energy = PET_STAT_CRITICAL;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "tI", " FEEd ");
    pet.fullness = PET_STAT_CRITICAL;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "HU", " FEEd ");
    pet.asleep = true;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "SL", " FEEd ");
    pet.stage = PET_STAGE_DEAD;
    pet.asleep = false;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "RI", "AGE  0");
}

static void test_render_bottom_line(void) {
    pet_state_t pet = baby();
    pet.fullness = 100;
    pet.happiness = 7;
    pet.energy = 42;
    CHECK_SCREEN(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, "SA", " FEEd ");
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_ACTION, "SA", " PLAY ");
    CHECK_SCREEN(&pet, PET_ACTION_LIGHTS, PET_PAGE_ACTION, "SA", " bEd  ");
    pet.asleep = true; // the lights action names what it would do now
    CHECK_SCREEN(&pet, PET_ACTION_LIGHTS, PET_PAGE_ACTION, "SL", "GEt UP");
    pet.asleep = false;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_ACTION, "SA", "StAtUS");
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_FULLNESS, "SA", "EA 100");
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_HAPPINESS, "SA", "HA   7");
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_ENERGY, "SA", "En  42");
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_REFUSED, "SA", "  no  ");

    pet.age_minutes = 2 * 24 * 60 + 500;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "SA", "CH   2");
    pet.stage = PET_STAGE_ADULT;
    pet.age_minutes = 45 * 24 * 60;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "SA", "Ad  45");
    pet.age_minutes = UINT32_MAX;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "SA", "Ad 999");
    pet.stage = PET_STAGE_DEAD;
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_FULLNESS, "RI", "AGE999");

    pet_init(&pet, T0);
    CHECK_SCREEN(&pet, PET_ACTION_STATUS, PET_PAGE_AGE, "O ", "EGG  0");
    // An out-of-range action or page still yields well-formed strings.
    CHECK_SCREEN(&pet, PET_ACTION_COUNT, PET_PAGE_ACTION, "O ", "      ");
    CHECK_SCREEN(&pet, PET_ACTION_FEED, (pet_page_t)99, "O ", "      ");
}

static void test_render_doze_and_indicators(void) {
    pet_state_t pet = baby();
    pet_screen_t s;
    pet_render(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, &s);
    CHECK(!s.bell && !s.sleep);
    // In low-energy mode the watch shows a sleeping pet whatever its mood...
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_DOZE, "SL", "      ");
    pet.fullness = 3;
    pet_render(&pet, PET_ACTION_FEED, PET_PAGE_DOZE, &s);
    CHECK(s.bell && !s.sleep); // ...but the bell still tells the truth.
    pet.asleep = true;
    pet_render(&pet, PET_ACTION_FEED, PET_PAGE_ACTION, &s);
    CHECK(s.bell && s.sleep);
    pet.stage = PET_STAGE_DEAD;
    pet.asleep = false;
    CHECK_SCREEN(&pet, PET_ACTION_PLAY, PET_PAGE_DOZE, "RI", "      ");
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

// ---- pet_load ----

static void test_load_no_file_means_a_new_egg(void) {
    pet_state_t pet = baby(), before = pet;
    uint8_t buf[PET_SAVE_SIZE] = {0};
    CHECK_EQ_INT(pet_load(false, false, buf, 0, &pet), PET_LOAD_NO_SAVE, "no file");
    CHECK(same(&pet, &before));
}

static void test_load_good_file(void) {
    pet_state_t saved = baby(), pet;
    uint8_t buf[PET_SAVE_SIZE];
    saved.fullness = 42;
    pet_serialize(&saved, buf);
    memset(&pet, 0, sizeof(pet));
    CHECK_EQ_INT(pet_load(true, true, buf, PET_SAVE_SIZE, &pet), PET_LOAD_OK, "good file");
    CHECK(same(&pet, &saved));

    // A longer file with the same version (fields appended later) still loads.
    memset(&pet, 0, sizeof(pet));
    CHECK_EQ_INT(pet_load(true, true, buf, PET_SAVE_SIZE + 8, &pet), PET_LOAD_OK, "longer file");
    CHECK(same(&pet, &saved));
}

// The point of #39: a file that exists is never treated as "no pet yet",
// whatever is wrong with it, so nothing overwrites it without the user asking.
static void test_load_existing_file_that_cannot_be_used_is_unreadable(void) {
    pet_state_t good = baby(), pet = good, before = pet;
    uint8_t buf[PET_SAVE_SIZE];
    pet_serialize(&good, buf);

    // The read itself failed (flash error), though the bytes would be fine.
    CHECK_EQ_INT(pet_load(true, false, buf, PET_SAVE_SIZE, &pet), PET_LOAD_UNREADABLE, "read failed");
    // Written by a newer firmware.
    buf[0] = PET_SAVE_VERSION + 1;
    CHECK_EQ_INT(pet_load(true, true, buf, PET_SAVE_SIZE, &pet), PET_LOAD_UNREADABLE, "newer version");
    buf[0] = PET_SAVE_VERSION;
    // Corrupt contents.
    buf[3] = PET_STAT_MAX + 1;
    CHECK_EQ_INT(pet_load(true, true, buf, PET_SAVE_SIZE, &pet), PET_LOAD_UNREADABLE, "corrupt");
    buf[3] = good.fullness;
    // Truncated.
    CHECK_EQ_INT(pet_load(true, true, buf, PET_SAVE_SIZE - 1, &pet), PET_LOAD_UNREADABLE, "truncated");
    // Empty, or the filesystem couldn't even look the file up: nothing was read.
    CHECK_EQ_INT(pet_load(true, false, buf, 0, &pet), PET_LOAD_UNREADABLE, "nothing read");
    CHECK(same(&pet, &before));
}

static void test_render_unreadable(void) {
    pet_screen_t s;
    memset(&s, 0x7f, sizeof(s));
    pet_render_unreadable(&s);
    CHECK(!s.has_sprite); // text, so it can't be taken for a pet
    CHECK(strcmp(s.top, "FILE ") == 0);
    CHECK(strcmp(s.top_fallback, "FI") == 0);
    CHECK(strcmp(s.bottom, "Error ") == 0);
    CHECK(!s.bell);
    CHECK(!s.sleep);
    check_screen_fits(&s);
}

// "State survives a reset": a pet restored from its save file and caught up
// to the clock is the pet that never stopped running, as long as the save is
// younger than the catch-up cap. The face saves often enough for that.
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
    CHECK(!pet_should_save(&saved, &pet, false));

    pet_advance_to(&pet, T0 + PET_SAVE_INTERVAL_SECONDS - 1);
    CHECK(!pet_should_save(&saved, &pet, false));
    pet_advance_to(&pet, T0 + PET_SAVE_INTERVAL_SECONDS);
    CHECK(pet_should_save(&saved, &pet, false));

    pet = saved;
    pet_advance_to(&pet, T0 + PET_SAVE_MIN_GAP_SECONDS);
    pet.asleep = true;
    CHECK(pet_should_save(&saved, &pet, false));
    pet.asleep = false;
    pet.stage = PET_STAGE_ADULT;
    CHECK(pet_should_save(&saved, &pet, false));

    pet = saved;
    pet_advance_to(&pet, T0 - 1); // clock went backward: keep the new anchor
    CHECK(pet_should_save(&saved, &pet, false));

    // Nothing about a dead pet changes with time, so once its death is on
    // file it is never written again.
    pet_state_t dead = saved;
    dead.stage = PET_STAGE_DEAD;
    dead.last_update = T0 + PET_SAVE_MIN_GAP_SECONDS;
    CHECK(pet_should_save(&saved, &dead, false));
    pet = dead;
    pet_advance_to(&pet, T0 + 10 * PET_SAVE_INTERVAL_SECONDS);
    CHECK(!pet_should_save(&dead, &pet, false));
    pet_advance_to(&pet, T0 - 1);
    CHECK(!pet_should_save(&dead, &pet, false));
}

// A change (an action, bed or get-up, a new stage) is worth a write, but not
// within PET_SAVE_MIN_GAP_SECONDS of the last one: it waits its turn.
static void test_a_change_waits_for_the_gap_since_the_last_save(void) {
    CHECK(PET_SAVE_MIN_GAP_SECONDS < PET_SAVE_INTERVAL_SECONDS);

    const pet_state_t saved = baby();
    pet_state_t pet = saved;

    // An action right after a save: not yet, then yes.
    CHECK(pet_should_save(&saved, &pet, true) == false);
    pet_advance_to(&pet, T0 + PET_SAVE_MIN_GAP_SECONDS - 1);
    CHECK(!pet_should_save(&saved, &pet, true));
    pet_advance_to(&pet, T0 + PET_SAVE_MIN_GAP_SECONDS);
    CHECK(pet_should_save(&saved, &pet, true));
    // Time alone, with nothing done to the pet, still waits for the interval.
    CHECK(!pet_should_save(&saved, &pet, false));

    // Sleep and stage changes wait the same way.
    pet = saved;
    pet.asleep = true;
    CHECK(!pet_should_save(&saved, &pet, false));
    pet_advance_to(&pet, T0 + PET_SAVE_MIN_GAP_SECONDS - 1);
    CHECK(!pet_should_save(&saved, &pet, false));
    pet.asleep = false;
    pet.stage = PET_STAGE_DEAD;
    CHECK(!pet_should_save(&saved, &pet, false));
    pet_advance_to(&pet, T0 + PET_SAVE_MIN_GAP_SECONDS);
    CHECK(pet_should_save(&saved, &pet, false));

    // A clock set backward is saved at once, whatever else is pending.
    pet = saved;
    pet_advance_to(&pet, T0 - 1);
    CHECK(pet_should_save(&saved, &pet, true));
}

// What the face does on every tick and every ALARM press, with the save
// file reduced to a counter.
typedef struct {
    pet_state_t pet;
    pet_state_t saved;
    bool unsaved_action;
    uint32_t writes;
} fake_face_t;

#define NO_PRESS PET_ACTION_COUNT

static void fake_face_step(fake_face_t *face, uint32_t now, pet_action_t press) {
    pet_advance_to(&face->pet, now);
    if (press != NO_PRESS && pet_apply_action(&face->pet, press) && pet_action_needs_save_note(press)) {
        face->unsaved_action = true;
    }
    if (pet_should_save(&face->saved, &face->pet, face->unsaved_action)) {
        face->saved = face->pet;
        face->unsaved_action = false;
        face->writes++;
    }
}

// bEd / GEt UP applies for as long as the pet isn't fully rested. Pressed
// once a second for an hour, it must not cost a flash write per press, and
// the last press must still reach the file.
static void test_an_hour_of_bed_presses_is_rate_limited_and_nothing_is_lost(void) {
    fake_face_t face = {.pet = baby()};
    uint32_t now = T0 + 5 * HOUR; // awake long enough to be tired
    pet_advance_to(&face.pet, now);
    face.saved = face.pet;

    const uint32_t presses = 3601; // odd: the pet ends up asleep
    for (uint32_t i = 0; i < presses; i++) fake_face_step(&face, ++now, PET_ACTION_LIGHTS);
    CHECK(face.pet.asleep);
    CHECK(face.writes > 0);
    CHECK(face.writes <= presses / PET_SAVE_MIN_GAP_SECONDS + 1);

    // The presses stop. Within one gap the file matches the pet.
    for (uint32_t i = 0; i < PET_SAVE_MIN_GAP_SECONDS; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK(face.saved.asleep);
    CHECK(!face.unsaved_action);
    const uint32_t settled = face.writes;
    for (uint32_t i = 0; i < 10 * PET_SAVE_MIN_GAP_SECONDS; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, settled, "writes once the presses stop");
}

// Feeding changes only a stat, so nothing but the face's "unsaved action"
// note says there is something to write. A feed right after a save waits,
// then is written once.
static void test_a_feed_right_after_a_save_is_written_one_gap_later(void) {
    fake_face_t face = {.pet = baby()};
    uint32_t now = T0 + 5 * HOUR; // long enough to be hungry
    pet_advance_to(&face.pet, now);
    face.saved = face.pet;

    fake_face_step(&face, ++now, PET_ACTION_FEED);
    CHECK(face.unsaved_action);
    CHECK_EQ_INT(face.writes, 0, "writes right after the feed");

    for (uint32_t i = 1; i < PET_SAVE_MIN_GAP_SECONDS - 1; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 0, "writes just before the gap is up");
    fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 1, "writes when the gap is up");
    CHECK(!face.unsaved_action);
    CHECK_EQ_INT(face.saved.fullness, face.pet.fullness, "fullness on file");

    for (uint32_t i = 0; i < 10 * PET_SAVE_MIN_GAP_SECONDS; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 1, "writes with nothing more done");
}

// bEd then GEt UP leaves the pet as it was saved, so there is nothing to
// write, however long the face then waits.
static void test_bed_then_get_up_right_after_a_save_writes_nothing(void) {
    fake_face_t face = {.pet = baby()};
    uint32_t now = T0 + 5 * HOUR; // awake long enough to be tired
    pet_advance_to(&face.pet, now);
    face.saved = face.pet;

    fake_face_step(&face, ++now, PET_ACTION_LIGHTS);
    CHECK(face.pet.asleep);
    fake_face_step(&face, ++now, PET_ACTION_LIGHTS);
    CHECK(!face.pet.asleep);

    for (uint32_t i = 0; i < 5 * PET_SAVE_MIN_GAP_SECONDS; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 0, "writes after bEd then GEt UP");
    CHECK(!face.unsaved_action);

    // The pet is still saved when the interval comes round.
    for (uint32_t i = 0; i < PET_SAVE_INTERVAL_SECONDS; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 1, "writes once the save interval has passed");
}

// A feed's note outlasts a bEd / GEt UP that follows it: the feed is still
// written when the gap is up.
static void test_a_feed_is_still_written_after_bed_then_get_up(void) {
    fake_face_t face = {.pet = baby()};
    uint32_t now = T0 + 5 * HOUR; // hungry and tired
    pet_advance_to(&face.pet, now);
    face.saved = face.pet;

    fake_face_step(&face, ++now, PET_ACTION_FEED);
    fake_face_step(&face, ++now, PET_ACTION_LIGHTS);
    fake_face_step(&face, ++now, PET_ACTION_LIGHTS);
    CHECK(!face.pet.asleep);
    CHECK_EQ_INT(face.writes, 0, "writes inside the gap");

    for (uint32_t i = 0; i < 5 * PET_SAVE_MIN_GAP_SECONDS; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 1, "writes after feed, bEd, GEt UP");
    CHECK_EQ_INT(face.saved.fullness, face.pet.fullness, "fullness on file");
}

// bEd alone is a change, and is written once when the gap is up.
static void test_bed_right_after_a_save_is_written_one_gap_later(void) {
    fake_face_t face = {.pet = baby()};
    uint32_t now = T0 + 5 * HOUR; // awake long enough to be tired
    pet_advance_to(&face.pet, now);
    face.saved = face.pet;

    fake_face_step(&face, ++now, PET_ACTION_LIGHTS);
    CHECK(face.pet.asleep);
    CHECK_EQ_INT(face.writes, 0, "writes right after bEd");

    for (uint32_t i = 1; i < PET_SAVE_MIN_GAP_SECONDS - 1; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 0, "writes just before the gap is up");
    fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 1, "writes when the gap is up");
    CHECK(face.saved.asleep);

    for (uint32_t i = 0; i < 5 * PET_SAVE_MIN_GAP_SECONDS; i++) fake_face_step(&face, ++now, NO_PRESS);
    CHECK_EQ_INT(face.writes, 1, "writes with nothing more done");
}

static void test_only_stat_actions_need_a_save_note(void) {
    CHECK(pet_action_needs_save_note(PET_ACTION_FEED));
    CHECK(pet_action_needs_save_note(PET_ACTION_PLAY));
    CHECK(!pet_action_needs_save_note(PET_ACTION_LIGHTS));
    CHECK(!pet_action_needs_save_note(PET_ACTION_STATUS));
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
    test_sprite_stages();
    test_sprite_moods();
    test_sprite_doze();
    test_sprites_differ();
    test_sprite_is_the_same_on_every_page();
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
    test_a_change_waits_for_the_gap_since_the_last_save();
    test_an_hour_of_bed_presses_is_rate_limited_and_nothing_is_lost();
    test_a_feed_right_after_a_save_is_written_one_gap_later();
    test_only_stat_actions_need_a_save_note();
    test_bed_then_get_up_right_after_a_save_writes_nothing();
    test_a_feed_is_still_written_after_bed_then_get_up();
    test_bed_right_after_a_save_is_written_one_gap_later();
    return check_summary("test_pet");
}

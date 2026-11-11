/*
 * MIT License
 *
 * Copyright (c) 2026 second-movement contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Virtual pet: state, time-driven decay, actions, save format and screen
// text. No hardware dependencies, so it also builds on a PC for the host
// tests in test/.

#ifndef PET_H_
#define PET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---- Tuning: every rate and threshold lives here. ----

#define PET_STAT_MAX 100
// A stat at or below this needs attention.
#define PET_STAT_CRITICAL 20
// The mood is "happy" when every stat is at least this.
#define PET_STAT_CONTENT 70

// Minutes for a stat to drop one point while awake. Fullness is the fastest:
// 100 -> 20 takes 80 * 6 min = 8 waking hours.
#define PET_AWAKE_FULLNESS_PERIOD 6
#define PET_AWAKE_HAPPINESS_PERIOD 8
#define PET_AWAKE_ENERGY_PERIOD 10
// Asleep, fullness and happiness drop three times slower, and energy rises
// one point every PET_ASLEEP_ENERGY_PERIOD minutes (0 -> 100 in 5 hours).
// The pet wakes by itself at full energy.
#define PET_ASLEEP_FULLNESS_PERIOD 18
#define PET_ASLEEP_HAPPINESS_PERIOD 24
#define PET_ASLEEP_ENERGY_PERIOD 3

#define PET_FEED_FULLNESS 30
#define PET_PLAY_HAPPINESS 25
#define PET_PLAY_ENERGY_COST 5

#define PET_EGG_MINUTES 5                   // egg -> baby
#define PET_ADULT_AGE_MINUTES (3 * 24 * 60) // baby -> adult
// Minutes in a row spent needing attention before the pet dies.
#define PET_DEATH_MINUTES (24 * 60)

// Longest stretch one update will live through. The face updates the pet
// every minute, so a bigger gap means the clock was set forward or the watch
// lost power, and the pet ages by this much at most. It is shorter than
// PET_DEATH_MINUTES, so a single time-set can't kill a pet that was fine.
#define PET_MAX_CATCHUP_SECONDS (12UL * 60 * 60)

// Save again once this much time has passed since the last save. Shorter
// than PET_MAX_CATCHUP_SECONDS, so a pet reloaded after a reset catches up
// exactly; long enough to cost only a few flash writes a day.
#define PET_SAVE_INTERVAL_SECONDS (6UL * 60 * 60)

// ---- State ----

typedef enum {
    PET_STAGE_EGG = 0,
    PET_STAGE_BABY,
    PET_STAGE_ADULT,
    PET_STAGE_DEAD,
} pet_stage_t;

typedef struct {
    uint32_t last_update;      // UTC unix time the state was last advanced to
    uint32_t age_minutes;      // stops counting at death
    uint16_t critical_minutes; // minutes in a row spent needing attention
    uint8_t stage;             // pet_stage_t
    uint8_t fullness;          // 0..PET_STAT_MAX, higher is better (as are the next two)
    uint8_t happiness;
    uint8_t energy;
    uint8_t remainder_seconds; // 0..59, time not yet counted as a whole minute
    bool asleep;
} pet_state_t;

// A fresh egg, laid at the given UTC time.
void pet_init(pet_state_t *pet, uint32_t now);

// True when a stat is critical: the bell. Energy doesn't count while asleep,
// since sleeping is the cure. An egg or a dead pet never needs attention.
bool pet_needs_attention(const pet_state_t *pet);

// Lives through elapsed_seconds (at most PET_MAX_CATCHUP_SECONDS). Returns
// true if at least one whole minute was counted, i.e. the state may differ.
// Within the cap, advancing by a then by b gives the same state as by a + b.
bool pet_advance(pet_state_t *pet, uint32_t elapsed_seconds);

// Advances to the UTC time `now`. A clock set backward counts as no time
// passed. Returns as pet_advance does.
bool pet_advance_to(pet_state_t *pet, uint32_t now);

// ---- Actions ----

typedef enum {
    PET_ACTION_FEED = 0,
    PET_ACTION_PLAY,
    PET_ACTION_LIGHTS, // lights out, or back on if asleep
    PET_ACTION_STATUS,
    PET_ACTION_COUNT,
} pet_action_t;

// Performs the action. Returns false, leaving the pet untouched, when it
// doesn't apply: anything but status on an egg or a dead pet; feeding or
// playing while asleep; feeding when full; playing when too tired or already
// at full happiness; lights out at full energy.
bool pet_apply_action(pet_state_t *pet, pet_action_t action);

// ---- Screen ----

// What the bottom line shows.
typedef enum {
    PET_PAGE_ACTION = 0, // the selected action's name
    PET_PAGE_FULLNESS,
    PET_PAGE_HAPPINESS,
    PET_PAGE_ENERGY,
    PET_PAGE_AGE,        // life stage and age in days
    PET_PAGE_REFUSED,    // the action didn't apply
    PET_PAGE_DOZE,       // low-energy mode: the watch itself is asleep
} pet_page_t;

// The page to show after ALARM performed `action` (applied = what
// pet_apply_action returned) while `page` was showing: the stat the action
// changed, the next status page, the action name again (lights), or the
// refusal.
pet_page_t pet_page_after_action(pet_action_t action, bool applied, pet_page_t page);

typedef struct {
    char top[6];          // 5 characters (space-padded), for the custom LCD
    char top_fallback[3]; // 2 characters, for the classic LCD
    char bottom[7];       // 6 characters
    bool bell;            // needs attention
    bool sleep;           // pet asleep
} pet_screen_t;

void pet_render(const pet_state_t *pet, pet_action_t action, pet_page_t page, pet_screen_t *screen);

// ---- Save file ----

#define PET_SAVE_VERSION 1
#define PET_SAVE_SIZE 17

// Writes PET_SAVE_SIZE bytes: a version byte, then the fields, little-endian.
void pet_serialize(const pet_state_t *pet, uint8_t buf[PET_SAVE_SIZE]);

// Reads a save file. Returns false, leaving *pet untouched, if it's too
// short, has an unknown version or holds out-of-range values.
bool pet_deserialize(const uint8_t *buf, size_t length, pet_state_t *pet);

// True when `pet` should be written out, given the copy last saved: its
// stage or sleep changed, the clock went backward, or
// PET_SAVE_INTERVAL_SECONDS have passed. (Save after every applied action
// too; that one is the caller's job.)
bool pet_should_save(const pet_state_t *saved, const pet_state_t *pet);

#endif // PET_H_

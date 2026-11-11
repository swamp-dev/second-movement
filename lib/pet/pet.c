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

#include <string.h>
#include "pet.h"

void pet_init(pet_state_t *pet, uint32_t now) {
    memset(pet, 0, sizeof(*pet));
    pet->last_update = now;
    pet->stage = PET_STAGE_EGG;
    pet->fullness = PET_STAT_MAX;
    pet->happiness = PET_STAT_MAX;
    pet->energy = PET_STAT_MAX;
}

static bool is_hatched_and_alive(const pet_state_t *pet) {
    return pet->stage == PET_STAGE_BABY || pet->stage == PET_STAGE_ADULT;
}

bool pet_needs_attention(const pet_state_t *pet) {
    if (!is_hatched_and_alive(pet)) return false;
    return pet->fullness <= PET_STAT_CRITICAL ||
           pet->happiness <= PET_STAT_CRITICAL ||
           (!pet->asleep && pet->energy <= PET_STAT_CRITICAL);
}

// Drops the stat by one point on every `period`-th minute of the pet's life.
// Keying on the age (not on when the update happens) is what makes the result
// independent of how the elapsed time is sliced.
static void decay(uint8_t *stat, uint32_t age_minutes, uint32_t period) {
    if (*stat > 0 && age_minutes % period == 0) (*stat)--;
}

static void live_one_minute(pet_state_t *pet) {
    if (pet->stage == PET_STAGE_DEAD) return;

    pet->age_minutes++;

    if (pet->stage == PET_STAGE_EGG) {
        if (pet->age_minutes >= PET_EGG_MINUTES) pet->stage = PET_STAGE_BABY;
        return;
    }

    if (pet->asleep) {
        decay(&pet->fullness, pet->age_minutes, PET_ASLEEP_FULLNESS_PERIOD);
        decay(&pet->happiness, pet->age_minutes, PET_ASLEEP_HAPPINESS_PERIOD);
        if (pet->energy < PET_STAT_MAX && pet->age_minutes % PET_ASLEEP_ENERGY_PERIOD == 0) pet->energy++;
        if (pet->energy >= PET_STAT_MAX) pet->asleep = false;
    } else {
        decay(&pet->fullness, pet->age_minutes, PET_AWAKE_FULLNESS_PERIOD);
        decay(&pet->happiness, pet->age_minutes, PET_AWAKE_HAPPINESS_PERIOD);
        decay(&pet->energy, pet->age_minutes, PET_AWAKE_ENERGY_PERIOD);
    }

    if (pet->stage == PET_STAGE_BABY && pet->age_minutes >= PET_ADULT_AGE_MINUTES) pet->stage = PET_STAGE_ADULT;

    if (pet_needs_attention(pet)) {
        if (++pet->critical_minutes >= PET_DEATH_MINUTES) {
            pet->stage = PET_STAGE_DEAD;
            pet->asleep = false;
        }
    } else {
        pet->critical_minutes = 0;
    }
}

bool pet_advance(pet_state_t *pet, uint32_t elapsed_seconds) {
    if (elapsed_seconds > PET_MAX_CATCHUP_SECONDS) elapsed_seconds = PET_MAX_CATCHUP_SECONDS;

    uint32_t seconds = pet->remainder_seconds + elapsed_seconds;
    uint32_t minutes = seconds / 60;
    pet->remainder_seconds = (uint8_t)(seconds % 60);

    for (uint32_t i = 0; i < minutes; i++) live_one_minute(pet);
    return minutes > 0;
}

bool pet_advance_to(pet_state_t *pet, uint32_t now) {
    uint32_t elapsed = now > pet->last_update ? now - pet->last_update : 0;
    pet->last_update = now;
    return pet_advance(pet, elapsed);
}

static uint8_t raised(uint8_t stat, uint8_t by) {
    return stat + by > PET_STAT_MAX ? PET_STAT_MAX : (uint8_t)(stat + by);
}

bool pet_apply_action(pet_state_t *pet, pet_action_t action) {
    if (action == PET_ACTION_STATUS) return true;
    if (!is_hatched_and_alive(pet)) return false;

    switch (action) {
        case PET_ACTION_FEED:
            if (pet->asleep || pet->fullness >= PET_STAT_MAX) return false;
            pet->fullness = raised(pet->fullness, PET_FEED_FULLNESS);
            return true;
        case PET_ACTION_PLAY:
            if (pet->asleep || pet->happiness >= PET_STAT_MAX || pet->energy <= PET_STAT_CRITICAL) return false;
            pet->happiness = raised(pet->happiness, PET_PLAY_HAPPINESS);
            pet->energy -= PET_PLAY_ENERGY_COST;
            return true;
        case PET_ACTION_LIGHTS:
            if (!pet->asleep && pet->energy >= PET_STAT_MAX) return false;
            pet->asleep = !pet->asleep;
            return true;
        default:
            return false;
    }
}

pet_page_t pet_page_after_action(pet_action_t action, bool applied, pet_page_t page) {
    if (!applied) return PET_PAGE_REFUSED;
    switch (action) {
        case PET_ACTION_FEED:
            return PET_PAGE_FULLNESS;
        case PET_ACTION_PLAY:
            return PET_PAGE_HAPPINESS;
        case PET_ACTION_STATUS:
            return (page >= PET_PAGE_FULLNESS && page < PET_PAGE_AGE) ? (pet_page_t)(page + 1) : PET_PAGE_FULLNESS;
        default:
            return PET_PAGE_ACTION;
    }
}

// Fills the 6-character bottom line: a label of up to 3 characters on the
// left and the value (shown as at most 999) right-aligned.
static void label_and_value(char bottom[7], const char *label, uint32_t value) {
    memcpy(bottom, "      ", 7);
    memcpy(bottom, label, strlen(label));
    if (value > 999) value = 999;
    int i = 5;
    do {
        bottom[i--] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
}

static void set_mood(pet_screen_t *screen, const char *top, const char *fallback) {
    strcpy(screen->top, top);
    strcpy(screen->top_fallback, fallback);
}

void pet_render(const pet_state_t *pet, pet_action_t action, pet_page_t page, pet_screen_t *screen) {
    // Only letters a 7-segment digit draws well: no I, K, M, V, W or X.
    static const char *const action_names[PET_ACTION_COUNT] = {"FEEd  ", "PLAY  ", "bEd   ", "StAtUS"};
    static const char *const stage_names[] = {"EGG", "bAb", "AdU", "AGE"};
    const bool alive = is_hatched_and_alive(pet);
    const uint32_t age_days = pet->age_minutes / (24 * 60);

    screen->bell = pet_needs_attention(pet);
    screen->sleep = alive && pet->asleep;

    if (pet->stage == PET_STAGE_DEAD) set_mood(screen, "RIP  ", "RI");
    else if (pet->stage == PET_STAGE_EGG) set_mood(screen, "EGG  ", "EG");
    else if (pet->asleep || page == PET_PAGE_DOZE) set_mood(screen, "SLEEP", "SL");
    else if (pet->fullness <= PET_STAT_CRITICAL) set_mood(screen, "HUNGr", "HU");
    else if (pet->energy <= PET_STAT_CRITICAL) set_mood(screen, "tIrEd", "tI");
    else if (pet->happiness <= PET_STAT_CRITICAL) set_mood(screen, "SAd  ", "SA");
    else if (pet->fullness >= PET_STAT_CONTENT && pet->happiness >= PET_STAT_CONTENT && pet->energy >= PET_STAT_CONTENT) set_mood(screen, "HAPPY", "HA");
    else set_mood(screen, "COOL ", "CO");

    strcpy(screen->bottom, "      ");
    if (page == PET_PAGE_DOZE) return;
    if (pet->stage == PET_STAGE_DEAD) {
        // The grave shows how old the pet got, whatever was selected.
        label_and_value(screen->bottom, "AGE", age_days);
        return;
    }
    switch (page) {
        case PET_PAGE_ACTION:
            if (action == PET_ACTION_LIGHTS && pet->asleep) strcpy(screen->bottom, "GEt UP");
            else if ((unsigned)action < PET_ACTION_COUNT) strcpy(screen->bottom, action_names[action]);
            break;
        case PET_PAGE_FULLNESS:
            label_and_value(screen->bottom, "Fd", pet->fullness);
            break;
        case PET_PAGE_HAPPINESS:
            label_and_value(screen->bottom, "HA", pet->happiness);
            break;
        case PET_PAGE_ENERGY:
            label_and_value(screen->bottom, "En", pet->energy);
            break;
        case PET_PAGE_AGE:
            label_and_value(screen->bottom, stage_names[pet->stage], age_days);
            break;
        case PET_PAGE_REFUSED:
            strcpy(screen->bottom, "  no  ");
            break;
        default:
            break;
    }
}

void pet_serialize(const pet_state_t *pet, uint8_t buf[PET_SAVE_SIZE]) {
    buf[0] = PET_SAVE_VERSION;
    buf[1] = pet->stage;
    buf[2] = pet->asleep ? 1 : 0;
    buf[3] = pet->fullness;
    buf[4] = pet->happiness;
    buf[5] = pet->energy;
    buf[6] = (uint8_t)pet->critical_minutes;
    buf[7] = (uint8_t)(pet->critical_minutes >> 8);
    for (int i = 0; i < 4; i++) {
        buf[8 + i] = (uint8_t)(pet->age_minutes >> (8 * i));
        buf[12 + i] = (uint8_t)(pet->last_update >> (8 * i));
    }
    buf[16] = pet->remainder_seconds;
}

static uint32_t read_u32(const uint8_t *buf) {
    return (uint32_t)buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
}

bool pet_deserialize(const uint8_t *buf, size_t length, pet_state_t *pet) {
    if (length < PET_SAVE_SIZE || buf[0] != PET_SAVE_VERSION) return false;
    if (buf[1] > PET_STAGE_DEAD || buf[2] > 1) return false;
    if (buf[3] > PET_STAT_MAX || buf[4] > PET_STAT_MAX || buf[5] > PET_STAT_MAX) return false;
    if (buf[16] >= 60) return false;

    memset(pet, 0, sizeof(*pet));
    pet->stage = buf[1];
    pet->asleep = buf[2];
    pet->fullness = buf[3];
    pet->happiness = buf[4];
    pet->energy = buf[5];
    pet->critical_minutes = (uint16_t)(buf[6] | buf[7] << 8);
    pet->age_minutes = read_u32(buf + 8);
    pet->last_update = read_u32(buf + 12);
    pet->remainder_seconds = buf[16];
    return true;
}

bool pet_should_save(const pet_state_t *saved, const pet_state_t *pet) {
    if (pet->stage != saved->stage || pet->asleep != saved->asleep) return true;
    if (pet->last_update < saved->last_update) return true;
    return pet->last_update - saved->last_update >= PET_SAVE_INTERVAL_SECONDS;
}

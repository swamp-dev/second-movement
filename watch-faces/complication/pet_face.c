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

#include <stdlib.h>
#include <string.h>
#include "pet_face.h"
#include "filesystem.h"
#include "watch.h"

static char PET_SAVE_FILE[] = "pet.bin";

static void _save(pet_face_state_t *state) {
    uint8_t buf[PET_SAVE_SIZE];
    pet_serialize(&state->pet, buf);
    filesystem_write_file(PET_SAVE_FILE, (char *)buf, sizeof(buf));
    // Counts as saved even if the write failed (say, the filesystem is full):
    // otherwise pet_should_save would stay true and retry on every tick. The
    // next change or save interval tries again.
    state->saved = state->pet;
}

// Brings the pet up to the present and saves it if enough has changed.
static void _catch_up(pet_face_state_t *state) {
    pet_advance_to(&state->pet, movement_get_utc_timestamp());
    if (pet_should_save(&state->saved, &state->pet)) _save(state);
}

static void _lay_egg(pet_face_state_t *state) {
    pet_init(&state->pet, movement_get_utc_timestamp());
    state->has_pet = true;
    state->action = PET_ACTION_FEED;
    state->page = PET_PAGE_ACTION;
    _save(state);
}

static void _draw(pet_face_state_t *state) {
    pet_screen_t screen;
    pet_render(&state->pet, (pet_action_t)state->action, (pet_page_t)state->page, &screen);
    state->drawn_age = state->pet.age_minutes;

    watch_display_text_with_fallback(WATCH_POSITION_TOP, screen.top, screen.top_fallback);
    watch_display_text(WATCH_POSITION_BOTTOM, screen.bottom);
    if (screen.bell) watch_set_indicator(WATCH_INDICATOR_BELL);
    else watch_clear_indicator(WATCH_INDICATOR_BELL);
    if (screen.sleep) watch_set_indicator(WATCH_INDICATOR_SLEEP);
    else watch_clear_indicator(WATCH_INDICATOR_SLEEP);
}

void pet_face_setup(uint8_t watch_face_index, void **context_ptr) {
    (void)watch_face_index;

    if (*context_ptr == NULL) {
        pet_face_state_t *state = malloc(sizeof(pet_face_state_t));
        memset(state, 0, sizeof(pet_face_state_t));
        *context_ptr = state;

        uint8_t buf[PET_SAVE_SIZE];
        if (filesystem_read_file(PET_SAVE_FILE, (char *)buf, sizeof(buf))) {
            int32_t size = filesystem_get_file_size(PET_SAVE_FILE);
            state->has_pet = pet_deserialize(buf, size > 0 ? (size_t)size : 0, &state->pet);
            state->saved = state->pet;
        }
        // With no usable save, the first egg is laid on first activation, not
        // here: setup runs at boot, often before the clock has been set.
    }
}

void pet_face_activate(void *context) {
    pet_face_state_t *state = (pet_face_state_t *)context;

    if (watch_sleep_animation_is_running()) watch_stop_sleep_animation();

    if (!state->has_pet) _lay_egg(state);
    else _catch_up(state);

    state->action = PET_ACTION_FEED;
    state->page = PET_PAGE_ACTION;
}

bool pet_face_loop(movement_event_t event, void *context) {
    pet_face_state_t *state = (pet_face_state_t *)context;
    bool dead = state->pet.stage == PET_STAGE_DEAD;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _draw(state);
            break;

        case EVENT_TICK:
            _catch_up(state);
            // Redraw when the pet has lived a minute since the last draw. The
            // minute may have been counted by pet_face_advise rather than
            // here, so compare ages instead of trusting this catch-up.
            if (state->pet.age_minutes != state->drawn_age) _draw(state);
            break;

        case EVENT_LOW_ENERGY_UPDATE:
            _catch_up(state);
            state->page = PET_PAGE_DOZE;
            _draw(state);
            if (!watch_sleep_animation_is_running()) watch_start_sleep_animation(1000);
            break;

        case EVENT_LIGHT_BUTTON_UP:
            if (dead) break;
            state->action = (state->action + 1) % PET_ACTION_COUNT;
            state->page = PET_PAGE_ACTION;
            _draw(state);
            break;

        case EVENT_ALARM_BUTTON_UP:
            if (dead) break;
            // Act on the pet as it is now, not as it was at the last tick.
            _catch_up(state);
            {
                bool applied = pet_apply_action(&state->pet, (pet_action_t)state->action);
                state->page = pet_page_after_action((pet_action_t)state->action, applied, (pet_page_t)state->page);
                if (applied && state->action != PET_ACTION_STATUS) _save(state);
            }
            _draw(state);
            break;

        case EVENT_ALARM_LONG_PRESS:
            // A long press, so a stray tap on the grave can't restart.
            if (dead) {
                _lay_egg(state);
                _draw(state);
            }
            break;

        case EVENT_LIGHT_LONG_PRESS:
            movement_illuminate_led();
            break;

        case EVENT_LIGHT_BUTTON_DOWN:
            // Don't light the LED on a short press: LIGHT picks the action.
            break;

        case EVENT_TIMEOUT:
            movement_move_to_face(0);
            break;

        default:
            return movement_default_loop_handler(event);
    }

    return true;
}

// Called by Movement every minute, whichever face is showing and in
// low-energy mode too. This is what keeps the pet living in real time when
// nobody is looking, and what makes a longer gap mean "the clock was set".
movement_watch_face_advisory_t pet_face_advise(void *context) {
    pet_face_state_t *state = (pet_face_state_t *)context;
    movement_watch_face_advisory_t advisory = {0};

    if (state->has_pet) _catch_up(state);
    return advisory;
}

void pet_face_resign(void *context) {
    pet_face_state_t *state = (pet_face_state_t *)context;
    if (pet_should_save(&state->saved, &state->pet)) _save(state);
}

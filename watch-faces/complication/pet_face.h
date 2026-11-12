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

#ifndef PET_FACE_H_
#define PET_FACE_H_

/*
 * VIRTUAL PET
 *
 * A pet that lives on the watch. It hatches from an egg, grows from child to
 * adult, and gets hungry, bored and tired as real time passes, whether or
 * not this face is showing (the watch updates it every minute). Left with a
 * critical stat for a day, it dies. If the clock jumps forward, the pet ages
 * 12 hours at most.
 *
 * The top line is its mood (HAPPY, COOL, HUNGr, tIrEd, SAd, SLEEP, EGG, RIP;
 * on the classic LCD the first two letters, and O for the egg). The bell
 * indicator means a stat is critical; the sleep indicator (custom LCD only)
 * means the pet is asleep. In low-energy mode the top line reads SLEEP (EGG
 * or RIP if that's the pet), the bottom line is blank and the sleep
 * animation runs.
 *
 * LIGHT        next action: FEEd, PLAY, bEd (GEt UP while asleep), StAtUS
 * ALARM        do it. The bottom line then shows the stat it changed (after
 *              bEd, the action becomes GEt UP), or "no" if the pet won't
 *              (full, too tired, asleep...). On StAtUS,
 *              each press shows the next of EA (how well fed), HA (happiness)
 *              and En (energy), each 0-100, then the life stage (EGG, CH
 *              for child, Ad for adult) with the age in days.
 * LIGHT (long) light the LED
 * ALARM (long) on the RIP screen only: hatch a new egg. RIP shows AGE and
 *              the days lived; short presses do nothing there.
 *
 * FILE / Error means a saved pet exists but can't be read (a flash error, or
 * a save from a newer firmware). The save is left alone and read again each
 * time the face is opened. ALARM (long) gives up on it and hatches a new egg.
 *
 * To spare the flash, what you do to the pet is saved at most once a minute.
 * A reset (a battery change, new firmware) can undo the last minute or two
 * of care.
 *
 * The rules and every rate live in lib/pet/pet.h.
 */

#include "movement.h"
#include "pet.h"

typedef struct {
    pet_state_t pet;
    pet_state_t saved;  // the pet as last written to the save file
    uint32_t drawn_age; // the pet's age_minutes when the screen was last drawn
    bool has_pet;       // false until a save was loaded or the first egg was laid
    bool unreadable;    // a save file exists but can't be used; it must not be overwritten
    bool unsaved_action; // a stat-only action (see pet_action_needs_save_note) was applied since the last save
    uint8_t action;     // pet_action_t selected with LIGHT
    uint8_t page;       // pet_page_t on the bottom line
} pet_face_state_t;

void pet_face_setup(uint8_t watch_face_index, void **context_ptr);
void pet_face_activate(void *context);
bool pet_face_loop(movement_event_t event, void *context);
void pet_face_resign(void *context);
movement_watch_face_advisory_t pet_face_advise(void *context);

#define pet_face ((const watch_face_t){ \
    pet_face_setup, \
    pet_face_activate, \
    pet_face_loop, \
    pet_face_resign, \
    pet_face_advise, \
})

#endif // PET_FACE_H_

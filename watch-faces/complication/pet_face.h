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
 * A pet that lives on the watch. It hatches from an egg, grows from baby to
 * adult, and gets hungry, bored and tired as real time passes, whether or
 * not this face is showing. Ignore it for too long and it dies.
 *
 * The top line is its mood (HAPPY, COOL, HUNGr, tIrEd, SAd, SLEEP, EGG, RIP).
 * The bell indicator means a stat is critical; the sleep indicator means the
 * pet is asleep.
 *
 * LIGHT        next action: FEEd, PLAY, bEd (GEt UP while asleep), StAtUS
 * ALARM        do it. The bottom line then shows the stat it changed, or
 *              "no" if the pet won't (full, too tired, asleep...). On StAtUS,
 *              each press shows the next of Fd, HA, En (0-100) and the life
 *              stage with the age in days.
 * LIGHT (long) light the LED
 * ALARM (long) on the RIP screen only: hatch a new egg
 *
 * The rules and every rate live in lib/pet/pet.h.
 */

#include "movement.h"
#include "pet.h"

typedef struct {
    pet_state_t pet;
    pet_state_t saved; // the pet as last written to the save file
    bool has_pet;      // false until a save was loaded or the first egg was laid
    uint8_t action;    // pet_action_t selected with LIGHT
    uint8_t page;      // pet_page_t on the bottom line
} pet_face_state_t;

void pet_face_setup(uint8_t watch_face_index, void **context_ptr);
void pet_face_activate(void *context);
bool pet_face_loop(movement_event_t event, void *context);
void pet_face_resign(void *context);

#define pet_face ((const watch_face_t){ \
    pet_face_setup, \
    pet_face_activate, \
    pet_face_loop, \
    pet_face_resign, \
    NULL, \
})

#endif // PET_FACE_H_

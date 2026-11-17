// Captures of moon_phase_face: the ten shapes it draws through a lunar month,
// in the northern and the southern hemisphere.

#include <stdio.h>

#include "harness.h"
#include "shots.h"
#include "watch_utility.h"
#include "complication/moon_phase_face.h"

// The face's own constants: the first new moon of 2000 and the mean month.
#define FIRST_MOON 947182440
#define LUNAR_SECONDS (29.53058770576 * 86400)
// A lunation in March 2026.
#define LUNATION 324

// A day of the lunar month well inside each shape (as test_face_moon_phase.c).
static const struct {
    const char *name;
    double day;
} shapes[] = {
    {"01-new", 0.5},           {"02-thin-waxing", 2.5},    {"03-thick-waxing", 5.0}, {"04-first-quarter", 7.4},
    {"05-waxing-gibbous", 11.0}, {"06-full", 14.8},        {"07-waning-gibbous", 18.5}, {"08-third-quarter", 22.1},
    {"09-thick-waning", 24.5}, {"10-thin-waning", 27.2},
};

int main(void) {
    for (size_t l = 0; l < SHOT_NUM_LCDS; l++) {
        for (int southern = 0; southern < 2; southern++) {
            for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
                uint32_t timestamp = (uint32_t)(FIRST_MOON + LUNATION * LUNAR_SECONDS + shapes[i].day * 86400);
                harness_init(SHOT_LCDS[l], watch_utility_date_time_from_unix_time(timestamp, 0),
                             harness_default_settings());
                if (southern) {
                    movement_location_t sydney = {.bit = {.latitude = -3387, .longitude = 15121}};
                    harness_write_file("location.u32", &sydney.reg, sizeof(sydney.reg));
                }
                harness_load_face(&moon_phase_face);

                char name[48];
                snprintf(name, sizeof(name), "%s-%s", southern ? "south" : "north", shapes[i].name);
                shot(name);
            }
        }
    }
    return 0;
}

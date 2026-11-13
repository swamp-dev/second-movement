// Host test harness for one watch face. It compiles the face's .c against
// host versions of the movement_* and watch_* calls a face makes, and drives
// it with the events movement.c would send: activate, ticks, button presses,
// background tasks, timeouts and low-energy updates.
//
// The harness models a watch with two faces. Index 0 is an empty "home" face
// that only calls movement_default_loop_handler, and index 1
// (HARNESS_FACE_INDEX) is the face under test. So a face that calls
// movement_move_to_face(0), or times out, leaves the screen as it would on
// the watch, and a short MODE press on the home face brings it back. There's
// no secondary face (MOVEMENT_SECONDARY_FACE_INDEX is 0), and the watch boots
// straight onto the face under test rather than onto face 0.
//
// Time is the RTC counter at 128 Hz, as on the watch. Every harness call that
// lets time pass steps that counter one count at a time and fires whatever
// movement would at each count: ticks, long-press timeouts, the top-of-minute
// alarm, scheduled background tasks, the resign and low-energy timeouts, the
// LED timeout and the end of a buzzer sequence.
//
// The display is test/fakes/fake_lcd: read it with harness_text, or assert
// with fake_lcd_shows.
//
// Only the calls the tested faces need are implemented. A face that calls
// anything else fails to link, which is the cue to add it here.
#ifndef HARNESS_H_
#define HARNESS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fake_lcd.h"
#include "movement.h"

#define HARNESS_FACE_INDEX 1

typedef enum {
    BUTTON_LIGHT,
    BUTTON_MODE,
    BUTTON_ALARM,
} harness_button_t;

typedef enum {
    PRESS_SHORT,  // down and up with no time between them
    PRESS_LONG,   // down, held until the long-press event (0.5 s and a count), then up
} harness_press_t;

typedef struct {
    bool on;
    uint8_t red, green, blue;
} harness_led_t;

// One sequence the buzzer started. A single note (watch_buzzer_play_note,
// movement_play_note) is logged as a one-note sequence.
#define HARNESS_MAX_SEQUENCE 64
typedef struct {
    int8_t sequence[HARNESS_MAX_SEQUENCE];  // copied up to and including its 0 terminator
    watch_buzzer_volume_t volume;
} harness_sound_t;

// The settings app_init in movement.c gives a new watch, from the defaults in
// movement_config.h, with the time zone set to UTC.
movement_settings_t harness_default_settings(void);

// Resets everything: the display (to `lcd`), the RTC (to `utc`, at the start
// of a second), movement's state, the buzzer and LED logs, and the face under
// test (its context is freed with free(); anything the context points to
// isn't). Call it before every test.
void harness_init(watch_lcd_type_t lcd, watch_date_time_t utc, movement_settings_t settings);

// Installs `face` at HARNESS_FACE_INDEX and boots it as app_setup does on
// first launch: setup for both faces, a 1 Hz tick, activate, then
// EVENT_ACTIVATE.
void harness_load_face(const watch_face_t *face);

// The context the face's setup stored.
void *harness_context(void);

// The face on screen: HARNESS_FACE_INDEX or 0.
uint8_t harness_current_face(void);

// Button edges. Each edge goes through movement's button handling and its
// own app_loop pass. A button pressed while the watch is in low-energy mode
// only wakes it (ALARM) or does nothing (LIGHT, MODE).
void harness_button_down(harness_button_t button);
void harness_button_up(harness_button_t button);
void harness_press(harness_button_t button, harness_press_t press);

// Lets time pass until `n` EVENT_TICKs have been sent, at whatever tick
// frequency is in force (a face may change it on the way). Aborts if the
// watch is in low-energy mode, where there are no ticks.
void harness_tick(uint32_t n);

// Lets `seconds` seconds pass.
void harness_advance(uint32_t seconds);

// Sets the RTC as movement_set_utc_timestamp does, which also refreshes the
// time-zone offset cache. The counter phase within the second is kept.
void harness_set_time(watch_date_time_t utc);
watch_date_time_t harness_utc_date_time(void);

// The text at a display position (see fake_lcd_text). The buffer is reused by
// the next call.
const char *harness_text(watch_position_t position);
bool harness_indicator(watch_indicator_t indicator);

uint8_t harness_tick_frequency(void);
bool harness_is_sleeping(void);

// The background task the face under test has scheduled, if any. `when`
// may be NULL.
bool harness_background_task(watch_date_time_t *when);

// Every sequence the buzzer started since harness_init or the last clear.
size_t harness_buzzer_count(void);
const harness_sound_t *harness_buzzer_log(size_t index);
void harness_buzzer_clear(void);
bool harness_buzzer_playing(void);

harness_led_t harness_led(void);

#endif // HARNESS_H_

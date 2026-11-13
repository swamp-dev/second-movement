// The hardware a face and movement touch, faked for the host: the RTC
// (counter, time, compare and periodic callbacks), the buzzer, the LED, the
// buttons and the sleep animation. The display is test/fakes/fake_lcd.c.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness_hw.h"
#include "watch.h"
#include "watch_utility.h"

static void _fail(const char *message) {
    fprintf(stderr, "harness: %s\n", message);
    abort();
}

// ---------------------------------------------------------------------------
// RTC. As on the watch, a 32-bit counter runs at 128 Hz and the time of day is
// derived from it. The formulas are the simulator's
// (watch-library/simulator/watch/watch_rtc.c:95-106): the second rolls over
// when the counter's low 7 bits reach 64, which is also when the 1 Hz
// periodic callback fires, so movement's 1 Hz tick lands on subsecond 0.
// Faster ticks fire where the hardware's do (watch_rtc.c:206-219 in the
// simulator): 2 Hz at 32 and 96, 4 Hz at 16, 48, 80 and 112, and so on, so a
// 4 Hz tick has subsecond 0 a sixteenth of a second after the second begins.
// A compare callback fires one count after its counter matches, as on the
// hardware (watch_rtc.c:246 in the simulator).

#define RTC_CNT_HZ 128
#define RTC_CNT_SUBSECOND_MASK (RTC_CNT_HZ - 1)
#define RTC_CNT_DIV 7
#define RTC_NUM_COMP 8   // WATCH_RTC_N_COMP_CB in the simulator

static rtc_counter_t counter;
static uint32_t reference_timestamp;

typedef struct {
    rtc_counter_t counter;
    watch_cb_t callback;
    bool enabled;
} comp_cb_t;

static comp_cb_t comp_callbacks[RTC_NUM_COMP];
// Indexed by PER_n, as watch-library/hardware/watch/watch_rtc.c:182-208 does:
// PER7 is 1 Hz, PER0 is 128 Hz.
static watch_cb_t periodic_callbacks[8];

void watch_rtc_set_unix_time(unix_timestamp_t unix_time) {
    reference_timestamp = unix_time - (counter >> RTC_CNT_DIV)
        - ((counter & RTC_CNT_SUBSECOND_MASK) >> (RTC_CNT_DIV - 1)) + 1;
}

unix_timestamp_t watch_rtc_get_unix_time(void) {
    return reference_timestamp + (counter >> RTC_CNT_DIV)
        + ((counter & RTC_CNT_SUBSECOND_MASK) >> (RTC_CNT_DIV - 1)) - 1;
}

void watch_rtc_set_date_time(rtc_date_time_t date_time) {
    watch_rtc_set_unix_time(watch_utility_date_time_to_unix_time(date_time, 0));
}

rtc_date_time_t watch_rtc_get_date_time(void) {
    return watch_utility_date_time_from_unix_time(watch_rtc_get_unix_time(), 0);
}

rtc_counter_t watch_rtc_get_counter(void) {
    return counter;
}

uint32_t watch_rtc_get_frequency(void) {
    return RTC_CNT_HZ;
}

uint32_t watch_rtc_get_ticks_per_minute(void) {
    return RTC_CNT_HZ * 60;
}

void watch_rtc_register_comp_callback_no_schedule(watch_cb_t callback, rtc_counter_t at, uint8_t index) {
    if (index >= RTC_NUM_COMP) _fail("compare callback index out of range");
    comp_callbacks[index] = (comp_cb_t){.counter = at, .callback = callback, .enabled = true};
}

void watch_rtc_disable_comp_callback_no_schedule(uint8_t index) {
    if (index >= RTC_NUM_COMP) _fail("compare callback index out of range");
    comp_callbacks[index].enabled = false;
}

// The harness checks every compare callback at every count, so there's no
// next one to program.
void watch_rtc_schedule_next_comp(void) {
}

void watch_rtc_register_periodic_callback(watch_cb_t callback, uint8_t frequency) {
    if (__builtin_popcount(frequency) != 1) return;
    periodic_callbacks[__builtin_clz((uint32_t)(frequency & 0xFF) << 24)] = callback;
}

void watch_rtc_disable_matching_periodic_callbacks(uint8_t mask) {
    for (int per_n = 0; per_n < 8; per_n++) {
        if (mask & (1 << per_n)) periodic_callbacks[per_n] = NULL;
    }
}

void hw_disable_periodic_callbacks(void) {
    watch_rtc_disable_matching_periodic_callbacks(0xFF);
}

// ---------------------------------------------------------------------------
// Buzzer. A sequence plays on a 64 Hz timer, so it ends a fixed number of RTC
// counts after it starts. Starting a sequence aborts the one playing, and an
// abort (or the natural end) calls that sequence's end callback, as
// watch-library/hardware/watch/watch_tcc.c:84-230 does.

#define BUZZER_TICK_COUNTS (RTC_CNT_HZ / 64)
#define MAX_SOUNDS 256

static watch_cb_t buzzer_cb_start_global;
static watch_cb_t buzzer_cb_stop_global;
static bool buzzer_active;
static rtc_counter_t buzzer_end;
static void (*buzzer_cb_finished)(void);
static harness_sound_t sounds[MAX_SOUNDS];
static size_t sound_count;


// How many 64 Hz ticks cb_watch_buzzer_seq (watch_tcc.c:113-148) takes to
// reach the end of `sequence`, following its repeat markers the same way.
static uint32_t _sequence_ticks(const int8_t *sequence) {
    uint32_t ticks = 0;
    int pos = 0;
    int repeat_counter = -1;
    int tone_ticks = 0;
    while (ticks < 1000000) {
        ticks++;
        if (tone_ticks != 0) {
            tone_ticks--;
            continue;
        }
        if (sequence[pos] < 0 && sequence[pos + 1]) {
            if (repeat_counter == -1) repeat_counter = sequence[pos + 1];
            else repeat_counter--;
            if (repeat_counter > 0) {
                if (pos > sequence[pos] * -2) pos += sequence[pos] * 2;
                else pos = 0;
            } else {
                pos += 2;
                repeat_counter = -1;
            }
        }
        if (sequence[pos] && sequence[pos + 1]) {
            tone_ticks = sequence[pos + 1] - 1;
            pos += 2;
        } else {
            return ticks;
        }
    }
    _fail("a buzzer sequence that never ends");
    return 0;
}

void watch_buzzer_register_global_callbacks(watch_cb_t cb_start, watch_cb_t cb_stop) {
    buzzer_cb_start_global = cb_start;
    buzzer_cb_stop_global = cb_stop;
}

void watch_buzzer_abort_sequence(void) {
    if (!buzzer_active) return;
    buzzer_active = false;
    if (buzzer_cb_stop_global) buzzer_cb_stop_global();
    if (buzzer_cb_finished) buzzer_cb_finished();
}

void watch_buzzer_play_sequence_with_volume(int8_t *note_sequence, void (*callback_on_end)(void), watch_buzzer_volume_t volume) {
    watch_buzzer_abort_sequence();
    if (buzzer_cb_start_global) buzzer_cb_start_global();

    if (sound_count == MAX_SOUNDS) _fail("more than 256 sounds logged; call harness_buzzer_clear");
    harness_sound_t *sound = &sounds[sound_count++];
    memset(sound, 0, sizeof(*sound));
    sound->volume = volume;
    // Copy note/duration pairs up to the terminating 0 note.
    size_t i = 0;
    while (note_sequence[i] != 0) {
        if (i + 2 >= HARNESS_MAX_SEQUENCE) _fail("a buzzer sequence longer than HARNESS_MAX_SEQUENCE");
        sound->sequence[i] = note_sequence[i];
        sound->sequence[i + 1] = note_sequence[i + 1];
        i += 2;
    }

    buzzer_active = true;
    buzzer_cb_finished = callback_on_end;
    buzzer_end = counter + _sequence_ticks(note_sequence) * BUZZER_TICK_COUNTS;
}

void watch_buzzer_play_sequence(int8_t *note_sequence, void (*callback_on_end)(void)) {
    watch_buzzer_play_sequence_with_volume(note_sequence, callback_on_end, WATCH_BUZZER_VOLUME_LOUD);
}

// watch_tcc.c:300-316: a note is a one-note sequence of duration_ms / 15 ticks.
void watch_buzzer_play_note_with_volume(watch_buzzer_note_t note, uint16_t duration_ms, watch_buzzer_volume_t volume) {
    static int8_t single_note_sequence[3];
    uint16_t duration = duration_ms / 15;
    if (duration > 127) duration = 127;
    single_note_sequence[0] = note;
    single_note_sequence[1] = (int8_t)duration;
    single_note_sequence[2] = 0;
    watch_buzzer_play_sequence_with_volume(single_note_sequence, NULL, volume);
}

void watch_buzzer_play_note(watch_buzzer_note_t note, uint16_t duration_ms) {
    watch_buzzer_play_note_with_volume(note, duration_ms, WATCH_BUZZER_VOLUME_LOUD);
}

size_t harness_buzzer_count(void) {
    return sound_count;
}

const harness_sound_t *harness_buzzer_log(size_t index) {
    if (index >= sound_count) _fail("harness_buzzer_log past the last sound");
    return &sounds[index];
}

void harness_buzzer_clear(void) {
    sound_count = 0;
}

bool harness_buzzer_playing(void) {
    return buzzer_active;
}

// ---------------------------------------------------------------------------
// LED, buttons, sleep animation.

static harness_led_t led;
static bool button_down[3];
static bool sleep_animation_running;

void watch_set_led_color_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    led = (harness_led_t){.on = (red | green | blue) != 0, .red = red, .green = green, .blue = blue};
}

void watch_set_led_off(void) {
    led = (harness_led_t){0};
}

harness_led_t harness_led(void) {
    return led;
}

void hw_set_button(harness_button_t button, bool down) {
    button_down[button] = down;
}

bool HAL_GPIO_BTN_LIGHT_read(void) {
    return button_down[BUTTON_LIGHT];
}

bool HAL_GPIO_BTN_MODE_read(void) {
    return button_down[BUTTON_MODE];
}

bool HAL_GPIO_BTN_ALARM_read(void) {
    return button_down[BUTTON_ALARM];
}

// On the watch these blink the colon on a timer; the harness only tracks
// whether the animation is on.
void watch_start_sleep_animation(uint32_t duration) {
    (void)duration;
    sleep_animation_running = true;
}

bool watch_sleep_animation_is_running(void) {
    return sleep_animation_running;
}

void watch_stop_sleep_animation(void) {
    sleep_animation_running = false;
}

// ---------------------------------------------------------------------------

void hw_reset(watch_date_time_t utc) {
    memset(comp_callbacks, 0, sizeof(comp_callbacks));
    memset(periodic_callbacks, 0, sizeof(periodic_callbacks));
    // Start where a second begins (see the RTC section above), so the first
    // 1 Hz tick comes a whole second after harness_init.
    counter = RTC_CNT_HZ / 2;
    watch_rtc_set_date_time(utc);

    buzzer_cb_start_global = NULL;
    buzzer_cb_stop_global = NULL;
    buzzer_active = false;
    buzzer_cb_finished = NULL;
    sound_count = 0;

    led = (harness_led_t){0};
    memset(button_down, 0, sizeof(button_down));
    sleep_animation_running = false;
}

bool hw_step(void) {
    bool fired = false;
    counter++;

    for (int per_n = 7; per_n >= 0; per_n--) {
        uint32_t period = 1u << per_n;
        if (periodic_callbacks[per_n] && (period == 1 || (counter & (period - 1)) == period / 2)) {
            periodic_callbacks[per_n]();
            fired = true;
        }
    }

    for (int i = 0; i < RTC_NUM_COMP; i++) {
        if (comp_callbacks[i].enabled && comp_callbacks[i].counter + 1 == counter) {
            comp_callbacks[i].enabled = false;
            comp_callbacks[i].callback();
            fired = true;
        }
    }

    if (buzzer_active && counter == buzzer_end) {
        watch_buzzer_abort_sequence();
        fired = true;
    }

    return fired;
}

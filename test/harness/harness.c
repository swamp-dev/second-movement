// The movement side of the face harness: movement.c's state, event loop and
// the movement_* calls a face makes, for a watch with two faces (the home
// face and the face under test, see harness.h).
//
// The code follows movement.c as of commit 88c2636 closely enough to diff
// against: each function names the lines it mirrors. What it leaves out:
// the USB shell, the accelerometer and thermistor (the harness watch has
// neither), the filesystem, and the hardware set-up in app_init/app_setup.
// When movement.c changes how it sends events, change the mirror here too.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness_hw.h"
#include "watch.h"
#include "watch_utility.h"
#include "zone_offset.h"
#include "zones.h"

// movement.c:26-28
#define MOVEMENT_LONG_PRESS_TICKS 64
#define MOVEMENT_REALLY_LONG_PRESS_TICKS 192
#define MOVEMENT_MAX_LONG_PRESS_TICKS 1280

#define MOVEMENT_NUM_FACES 2
#define MOVEMENT_SECONDARY_FACE_INDEX 0

// movement_config.h:63-109, the defaults app_init uses for a new watch.
#define MOVEMENT_DEFAULT_RED_COLOR 0x0
#define MOVEMENT_DEFAULT_GREEN_COLOR 0xF
#define MOVEMENT_DEFAULT_BLUE_COLOR 0x0
#define MOVEMENT_DEFAULT_IMPERIAL_UNITS true
#define MOVEMENT_DEFAULT_24H_MODE false
#define MOVEMENT_DEFAULT_BUTTON_SOUND true
#define MOVEMENT_DEFAULT_BUTTON_VOLUME WATCH_BUZZER_VOLUME_SOFT
#define MOVEMENT_DEFAULT_SIGNAL_VOLUME WATCH_BUZZER_VOLUME_LOUD
#define MOVEMENT_DEFAULT_ALARM_VOLUME WATCH_BUZZER_VOLUME_LOUD
#define MOVEMENT_DEFAULT_TIMEOUT_INTERVAL 0
#define MOVEMENT_DEFAULT_LOW_ENERGY_INTERVAL 2
#define MOVEMENT_DEFAULT_LED_DURATION 1

// movement.c:61-129
static movement_state_t movement_state;
static void *watch_face_contexts[MOVEMENT_NUM_FACES];
static watch_date_time_t scheduled_tasks[MOVEMENT_NUM_FACES];
static watch_face_t watch_faces[MOVEMENT_NUM_FACES];
static const int32_t movement_le_inactivity_deadlines[8] = {INT_MAX, 600, 3600, 7200, 21600, 43200, 86400, 604800};
static const int16_t movement_timeout_inactivity_deadlines[4] = {60, 120, 300, 1800};

static const uint32_t _movement_mode_button_events_mask = 0b11111 << EVENT_MODE_BUTTON_DOWN;
static const uint32_t _movement_light_button_events_mask = 0b11111 << EVENT_LIGHT_BUTTON_DOWN;
static const uint32_t _movement_alarm_button_events_mask = 0b11111 << EVENT_ALARM_BUTTON_DOWN;
#define _movement_button_events_mask \
    (_movement_mode_button_events_mask | _movement_light_button_events_mask | _movement_alarm_button_events_mask)

typedef struct {
    movement_event_type_t down_event;
    watch_cb_t cb_longpress;
    movement_timeout_index_t timeout_index;
    bool is_down;
    rtc_counter_t down_timestamp;
} movement_button_t;

typedef struct {
    uint32_t pending_events;
    bool turn_led_off;
    bool has_pending_sequence;
    bool enter_sleep_mode;
    bool exit_sleep_mode;
    bool is_sleeping;
    uint8_t subsecond;
    rtc_counter_t minute_counter;
    bool minute_alarm_fired;
    bool is_buzzing;
    uint8_t pending_sequence_priority;
    bool schedule_next_comp;
    movement_button_t mode_button;
    movement_button_t light_button;
    movement_button_t alarm_button;
    uint32_t passthrough_events;
} movement_volatile_state_t;

static movement_volatile_state_t movement_volatile_state;
static int8_t *_pending_sequence;

// movement.c:116-127
static int8_t alarm_tune[] = {
    BUZZER_NOTE_C8, 3,
    BUZZER_NOTE_REST, 4,
    BUZZER_NOTE_C8, 3,
    BUZZER_NOTE_REST, 4,
    BUZZER_NOTE_C8, 3,
    BUZZER_NOTE_REST, 4,
    BUZZER_NOTE_C8, 5,
    BUZZER_NOTE_REST, 38,
    -8, 9,
    0
};

static int8_t _movement_dst_offset_cache[NUM_ZONE_NAMES];
#define TIMEZONE_DOES_NOT_OBSERVE (-127)

// Ticks sent since harness_init, for harness_tick.
static uint32_t tick_count;

static void cb_mode_btn_interrupt(void);
static void cb_light_btn_interrupt(void);
static void cb_alarm_btn_interrupt(void);
static void cb_alarm_btn_extwake(void);
static void cb_minute_alarm_fired(void);
static void cb_tick(void);
static void cb_mode_btn_timeout_interrupt(void);
static void cb_light_btn_timeout_interrupt(void);
static void cb_alarm_btn_timeout_interrupt(void);
static void cb_led_timeout_interrupt(void);
static void cb_resign_timeout_interrupt(void);
static void cb_sleep_timeout_interrupt(void);
static void cb_buzzer_start(void);
static void cb_buzzer_stop(void);

// ---------------------------------------------------------------------------
// The home face at index 0: a blank screen that does what any face does by
// default, so MODE moves on to the face under test.

static void home_setup(uint8_t watch_face_index, void **context_ptr) {
    (void)watch_face_index;
    (void)context_ptr;
}

static void home_activate(void *context) {
    (void)context;
}

static bool home_loop(movement_event_t event, void *context) {
    (void)context;
    return movement_default_loop_handler(event);
}

static void home_resign(void *context) {
    (void)context;
}

static const watch_face_t home_face = {home_setup, home_activate, home_loop, home_resign, NULL};

// ---------------------------------------------------------------------------
// movement.c:160-170
static udatetime_t _movement_convert_date_time_to_udate(watch_date_time_t date_time) {
    return (udatetime_t) {
        .date.dayofmonth = date_time.unit.day,
        .date.dayofweek = dayofweek(UYEAR_FROM_YEAR(date_time.unit.year + WATCH_RTC_REFERENCE_YEAR), date_time.unit.month, date_time.unit.day),
        .date.month = date_time.unit.month,
        .date.year = UYEAR_FROM_YEAR(date_time.unit.year + WATCH_RTC_REFERENCE_YEAR),
        .time.hour = date_time.unit.hour,
        .time.minute = date_time.unit.minute,
        .time.second = date_time.unit.second
    };
}

// movement.c:172-175, as it is: it hands out backup register numbers as
// volumes.
static watch_buzzer_volume_t _movement_get_buzzer_volume(movement_buzzer_priority_t priority) {
    (void)priority;
    if (movement_state.next_available_backup_register >= 7) return 0;
    return movement_state.next_available_backup_register++;
}

// movement.c:185-218
static void _movement_set_top_of_minute_alarm(void) {
    uint32_t counter = watch_rtc_get_counter();
    uint32_t next_minute_counter;
    watch_date_time_t date_time = watch_rtc_get_date_time();
    uint32_t freq = watch_rtc_get_frequency();
    uint32_t half_freq = freq >> 1;
    uint32_t subsecond_mask = freq - 1;
    uint32_t ticks_per_minute = watch_rtc_get_ticks_per_minute();

    next_minute_counter = counter & (~subsecond_mask);
    next_minute_counter += (counter & subsecond_mask) >= half_freq ? half_freq : -half_freq;
    next_minute_counter += (60 - date_time.unit.second) * freq;

    if (next_minute_counter == counter) {
        next_minute_counter += ticks_per_minute;
    }

    while ((next_minute_counter - counter) > ticks_per_minute) {
        next_minute_counter += ticks_per_minute;
    }

    movement_volatile_state.minute_counter = next_minute_counter;

    watch_rtc_register_comp_callback_no_schedule(cb_minute_alarm_fired, next_minute_counter, MINUTE_TIMEOUT);
    movement_volatile_state.schedule_next_comp = true;
}

// movement.c:220-239
static bool _movement_update_dst_offset_cache(void) {
    udatetime_t udate_time = _movement_convert_date_time_to_udate(watch_rtc_get_date_time());
    bool dst_changed = false;

    for (uint8_t i = 0; i < NUM_ZONE_NAMES; i++) {
        if (zone_defns[i].rules_len) {
            int8_t new_offset = zone_offset_at_utc(&zone_defns[i], udate_time) / OFFSET_INCREMENT;
            if (_movement_dst_offset_cache[i] != new_offset) {
                _movement_dst_offset_cache[i] = new_offset;
                dst_changed = true;
            }
        } else {
            _movement_dst_offset_cache[i] = TIMEZONE_DOES_NOT_OBSERVE;
        }
    }

    return dst_changed;
}

// movement.c:241-264
static void _movement_reset_inactivity_countdown(void) {
    rtc_counter_t counter = watch_rtc_get_counter();
    uint32_t freq = watch_rtc_get_frequency();

    watch_rtc_register_comp_callback_no_schedule(
        cb_resign_timeout_interrupt,
        counter + movement_timeout_inactivity_deadlines[movement_state.settings.bit.to_interval] * freq,
        RESIGN_TIMEOUT
    );

    movement_volatile_state.enter_sleep_mode = false;

    if (movement_state.settings.bit.le_interval == 0) {
        watch_rtc_disable_comp_callback_no_schedule(SLEEP_TIMEOUT);
    } else {
        watch_rtc_register_comp_callback_no_schedule(
            cb_sleep_timeout_interrupt,
            counter + movement_le_inactivity_deadlines[movement_state.settings.bit.le_interval] * freq,
            SLEEP_TIMEOUT
        );
    }

    movement_volatile_state.schedule_next_comp = true;
}

// movement.c:266-270
static void _movement_disable_inactivity_countdown(void) {
    watch_rtc_disable_comp_callback_no_schedule(RESIGN_TIMEOUT);
    watch_rtc_disable_comp_callback_no_schedule(SLEEP_TIMEOUT);
    movement_volatile_state.schedule_next_comp = true;
}

// movement.c:272-277
static void _movement_renew_top_of_minute_alarm(void) {
    movement_volatile_state.minute_counter += watch_rtc_get_ticks_per_minute();
    watch_rtc_register_comp_callback_no_schedule(cb_minute_alarm_fired, movement_volatile_state.minute_counter, MINUTE_TIMEOUT);
    movement_volatile_state.schedule_next_comp = true;
}

// movement.c:297-363
static void _movement_handle_button_presses(uint32_t pending_events) {
    bool any_up = false;
    bool any_down = false;
    bool any_long = false;

    movement_button_t *buttons[3] = {
        &movement_volatile_state.mode_button,
        &movement_volatile_state.light_button,
        &movement_volatile_state.alarm_button
    };

    uint32_t button_events_masks[3] = {
        _movement_mode_button_events_mask,
        _movement_light_button_events_mask,
        _movement_alarm_button_events_mask,
    };

    for (uint8_t i = 0; i < 3; i++) {
        movement_button_t *button = buttons[i];

        if (pending_events & (1 << button->down_event)) {
            watch_rtc_register_comp_callback_no_schedule(button->cb_longpress, button->down_timestamp + MOVEMENT_LONG_PRESS_TICKS, button->timeout_index);
            any_down = true;
            movement_volatile_state.passthrough_events &= ~button_events_masks[i];
        }

        if (pending_events & (1 << (button->down_event + 2))) {
            watch_rtc_register_comp_callback_no_schedule(button->cb_longpress, button->down_timestamp + MOVEMENT_REALLY_LONG_PRESS_TICKS, button->timeout_index);
            any_long = true;
        }

        if (pending_events & (1 << (button->down_event + 4))) {
            watch_rtc_register_comp_callback_no_schedule(button->cb_longpress, button->down_timestamp + MOVEMENT_MAX_LONG_PRESS_TICKS, button->timeout_index);
            any_long = true;
        }

        if (pending_events & ((1 << (button->down_event + 1)) | (1 << (button->down_event + 3)))) {
            watch_rtc_disable_comp_callback_no_schedule(button->timeout_index);
            any_up = true;
        }
    }

    if (any_down) {
        watch_buzzer_abort_sequence();
        if (movement_state.light_on) {
            movement_illuminate_led();
        }
    }

    if (any_down || any_up || any_long) {
        _movement_reset_inactivity_countdown();
        movement_volatile_state.schedule_next_comp = true;
    }
}

// movement.c:365-389
static void _movement_handle_top_of_minute(void) {
    watch_date_time_t date_time = watch_rtc_get_date_time();

    if (date_time.unit.minute % 30 == 0) {
        _movement_update_dst_offset_cache();
    }

    for (uint8_t i = 0; i < MOVEMENT_NUM_FACES; i++) {
        if (watch_faces[i].advise != NULL) {
            movement_watch_face_advisory_t advisory = watch_faces[i].advise(watch_face_contexts[i]);
            if (advisory.wants_background_task) {
                movement_event_t background_event = { EVENT_BACKGROUND_TASK, 0 };
                watch_faces[i].loop(background_event, watch_face_contexts[i]);
            }
        }
    }
}

// movement.c:391-416
static void _movement_handle_scheduled_tasks(void) {
    watch_date_time_t date_time = watch_rtc_get_date_time();
    uint8_t num_active_tasks = 0;

    for (uint8_t i = 0; i < MOVEMENT_NUM_FACES; i++) {
        if (scheduled_tasks[i].reg) {
            if (scheduled_tasks[i].reg <= date_time.reg) {
                scheduled_tasks[i].reg = 0;
                movement_event_t background_event = { EVENT_BACKGROUND_TASK, 0 };
                watch_faces[i].loop(background_event, watch_face_contexts[i]);
                if (scheduled_tasks[i].reg) {
                    num_active_tasks++;
                }
            } else {
                num_active_tasks++;
            }
        }
    }

    if (num_active_tasks == 0) {
        movement_state.has_scheduled_background_task = false;
    } else {
        _movement_reset_inactivity_countdown();
    }
}

// ---------------------------------------------------------------------------
// The movement_* API.

// movement.c:418-436
void movement_request_tick_frequency(uint8_t freq) {
    if (freq == 0 || __builtin_popcount(freq) != 1) freq = 1;

    watch_rtc_disable_matching_periodic_callbacks(0xFF);

    uint32_t tmp = (freq & 0xFF) << 24;
    uint8_t per_n = __builtin_clz(tmp);

    movement_state.tick_frequency = freq;
    movement_state.tick_pern = per_n;

    watch_rtc_register_periodic_callback(cb_tick, freq);
}

// movement.c:438-458
void movement_illuminate_led(void) {
    if (movement_state.settings.bit.led_duration != 0b111) {
        movement_state.light_on = true;
        watch_set_led_color_rgb(movement_state.settings.bit.led_red_color | movement_state.settings.bit.led_red_color << 4,
                                movement_state.settings.bit.led_green_color | movement_state.settings.bit.led_green_color << 4,
                                movement_state.settings.bit.led_blue_color | movement_state.settings.bit.led_blue_color << 4);
        if (movement_state.settings.bit.led_duration != 0) {
            rtc_counter_t counter = watch_rtc_get_counter();
            uint32_t freq = watch_rtc_get_frequency();
            watch_rtc_register_comp_callback_no_schedule(
                cb_led_timeout_interrupt,
                counter + (movement_state.settings.bit.led_duration * 2 - 1) * freq,
                LED_TIMEOUT
            );
            movement_volatile_state.schedule_next_comp = true;
        }
    }
}

// movement.c:460-467
void movement_force_led_on(uint8_t red, uint8_t green, uint8_t blue) {
    movement_state.light_on = true;
    watch_set_led_color_rgb(red, green, blue);
    watch_rtc_disable_comp_callback_no_schedule(LED_TIMEOUT);
    movement_volatile_state.schedule_next_comp = true;
}

// movement.c:469-475
void movement_force_led_off(void) {
    movement_state.light_on = false;
    watch_rtc_disable_comp_callback_no_schedule(LED_TIMEOUT);
    movement_volatile_state.schedule_next_comp = true;
    watch_set_led_off();
}

// movement.c:477-503
bool movement_default_loop_handler(movement_event_t event) {
    switch (event.event_type) {
        case EVENT_MODE_BUTTON_UP:
            movement_move_to_next_face();
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            movement_illuminate_led();
            break;
        case EVENT_LIGHT_BUTTON_UP:
        case EVENT_LIGHT_LONG_UP:
            if (movement_state.settings.bit.led_duration == 0) {
                movement_force_led_off();
            }
            break;
        case EVENT_MODE_LONG_PRESS:
            if (MOVEMENT_SECONDARY_FACE_INDEX && movement_state.current_face_idx == 0) {
                movement_move_to_face(MOVEMENT_SECONDARY_FACE_INDEX);
            } else {
                movement_move_to_face(0);
            }
            break;
        default:
            break;
    }

    return true;
}

// movement.c:505-518
void movement_move_to_face(uint8_t watch_face_index) {
    movement_state.watch_face_changed = true;
    movement_state.next_face_idx = watch_face_index;
}

void movement_move_to_next_face(void) {
    uint16_t face_max;
    if (MOVEMENT_SECONDARY_FACE_INDEX) {
        face_max = (movement_state.current_face_idx < (int16_t)MOVEMENT_SECONDARY_FACE_INDEX) ? MOVEMENT_SECONDARY_FACE_INDEX : MOVEMENT_NUM_FACES;
    } else {
        face_max = MOVEMENT_NUM_FACES;
    }
    movement_move_to_face((movement_state.current_face_idx + 1) % face_max);
}

// movement.c:520-546
void movement_schedule_background_task(watch_date_time_t date_time) {
    movement_schedule_background_task_for_face(movement_state.current_face_idx, date_time);
}

void movement_cancel_background_task(void) {
    movement_cancel_background_task_for_face(movement_state.current_face_idx);
}

void movement_schedule_background_task_for_face(uint8_t watch_face_index, watch_date_time_t date_time) {
    watch_date_time_t now = watch_rtc_get_date_time();
    if (date_time.reg > now.reg) {
        movement_state.has_scheduled_background_task = true;
        scheduled_tasks[watch_face_index].reg = date_time.reg;
    }
}

void movement_cancel_background_task_for_face(uint8_t watch_face_index) {
    scheduled_tasks[watch_face_index].reg = 0;
    bool other_tasks_scheduled = false;
    for (uint8_t i = 0; i < MOVEMENT_NUM_FACES; i++) {
        if (scheduled_tasks[i].reg != 0) {
            other_tasks_scheduled = true;
            break;
        }
    }
    movement_state.has_scheduled_background_task = other_tasks_scheduled;
}

// movement.c:548-555
void movement_request_sleep(void) {
    movement_volatile_state.enter_sleep_mode = true;
}

void movement_request_wake(void) {
    movement_volatile_state.exit_sleep_mode = true;
    _movement_reset_inactivity_countdown();
}

// movement.c:557-564
static void cb_buzzer_start(void) {
    movement_volatile_state.is_buzzing = true;
}

static void cb_buzzer_stop(void) {
    movement_volatile_state.is_buzzing = false;
    movement_volatile_state.pending_sequence_priority = 0;
}

// movement.c:618-636
void movement_play_sequence(int8_t *note_sequence, movement_buzzer_priority_t priority) {
    if (priority < movement_volatile_state.pending_sequence_priority) {
        return;
    }

    movement_volatile_state.pending_sequence_priority = priority;

    if (movement_volatile_state.is_sleeping) {
        _pending_sequence = note_sequence;
        movement_volatile_state.has_pending_sequence = true;
        movement_volatile_state.exit_sleep_mode = true;
    } else {
        watch_buzzer_play_sequence_with_volume(note_sequence, NULL, _movement_get_buzzer_volume(priority));
    }
}

// movement.c:566-578
void movement_play_note(watch_buzzer_note_t note, uint16_t duration_ms) {
    static int8_t single_note_sequence[3];

    single_note_sequence[0] = note;
    uint16_t duration = duration_ms / 15;
    if (duration > 127) duration = 127;
    single_note_sequence[1] = (int8_t)duration;
    single_note_sequence[2] = 0;

    movement_play_sequence(single_note_sequence, BUZZER_PRIORITY_BUTTON);
}

// movement.c:584-586
void movement_play_alarm(void) {
    movement_play_sequence(alarm_tune, BUZZER_PRIORITY_ALARM);
}

// movement.c:644-658
int32_t movement_get_current_timezone_offset_for_zone(uint8_t zone_index) {
    int8_t cached_dst_offset = _movement_dst_offset_cache[zone_index];

    if (cached_dst_offset == TIMEZONE_DOES_NOT_OBSERVE) {
        return (int32_t)zone_defns[zone_index].offset_inc_minutes * OFFSET_INCREMENT * 60;
    } else {
        return (int32_t)cached_dst_offset * OFFSET_INCREMENT * 60;
    }
}

int32_t movement_get_current_timezone_offset(void) {
    return movement_get_current_timezone_offset_for_zone(movement_state.settings.bit.time_zone);
}

// movement.c:688-690
watch_date_time_t movement_get_utc_date_time(void) {
    return watch_rtc_get_date_time();
}

// movement.c:714-716
uint32_t movement_get_utc_timestamp(void) {
    return watch_rtc_get_unix_time();
}

// movement.c:734-744
void movement_set_utc_timestamp(uint32_t timestamp) {
    watch_rtc_set_unix_time(timestamp);
    _movement_set_top_of_minute_alarm();
    _movement_update_dst_offset_cache();
}

// movement.c:747-761
bool movement_button_should_sound(void) {
    return movement_state.settings.bit.button_should_sound;
}

watch_buzzer_volume_t movement_button_volume(void) {
    return movement_state.settings.bit.button_volume;
}

// movement.c:848-893: with no accelerometer, both report that tap detection
// isn't available.
bool movement_enable_tap_detection_if_available(bool enable_double_tap) {
    (void)enable_double_tap;
    return false;
}

bool movement_disable_tap_detection_if_available(void) {
    return false;
}

// ---------------------------------------------------------------------------
// The event loop.

// movement.c:1081-1191, without the hardware set-up. On first launch the
// harness has already cleared the contexts and tasks (harness_init).
static void _app_setup(void) {
    if (!movement_volatile_state.is_sleeping) {
        movement_request_tick_frequency(1);

        for (uint8_t i = 0; i < MOVEMENT_NUM_FACES; i++) {
            watch_faces[i].setup(i, &watch_face_contexts[i]);
        }

        watch_faces[movement_state.current_face_idx].activate(watch_face_contexts[movement_state.current_face_idx]);
        movement_volatile_state.pending_events |= 1 << EVENT_ACTIVATE;
    }
}

// One pass of the loop in _sleep_mode_app_loop (movement.c:1193-1233): the
// watch runs one each time it wakes in low-energy mode (on entry, at the top
// of the minute, on a wake request). Returns true when the watch leaves
// low-energy mode. Between passes watch_enter_sleep_mode stops the ticks;
// its app_setup call on waking does nothing while is_sleeping is set.
static bool _sleep_mode_pass(void) {
    if (movement_volatile_state.exit_sleep_mode) {
        movement_volatile_state.exit_sleep_mode = false;
        movement_volatile_state.is_sleeping = false;
        return true;
    }

    if (movement_volatile_state.minute_alarm_fired) {
        movement_volatile_state.minute_alarm_fired = false;
        _movement_renew_top_of_minute_alarm();
        _movement_handle_top_of_minute();
    }

    movement_event_t event;
    event.event_type = EVENT_LOW_ENERGY_UPDATE;
    event.subsecond = 0;
    watch_faces[movement_state.current_face_idx].loop(event, watch_face_contexts[movement_state.current_face_idx]);

    if (movement_volatile_state.exit_sleep_mode) {
        movement_volatile_state.exit_sleep_mode = false;
        movement_volatile_state.is_sleeping = false;
        return true;
    }

    movement_volatile_state.schedule_next_comp = false;
    hw_disable_periodic_callbacks();
    return false;
}

// What app_loop does once _sleep_mode_app_loop returns (movement.c:1369-1386).
static void _wake_from_sleep_mode(void) {
    _app_setup();

    if (movement_volatile_state.has_pending_sequence) {
        movement_volatile_state.has_pending_sequence = false;
        watch_buzzer_play_sequence_with_volume(_pending_sequence, movement_request_sleep, _movement_get_buzzer_volume(movement_volatile_state.pending_sequence_priority));
        _pending_sequence = NULL;
    }
}

// movement.c:1237-1264
static void _switch_face(void) {
    const watch_face_t *wf = &watch_faces[movement_state.current_face_idx];

    wf->resign(watch_face_contexts[movement_state.current_face_idx]);
    movement_state.current_face_idx = movement_state.next_face_idx;
    wf = &watch_faces[movement_state.current_face_idx];
    watch_clear_display();
    movement_request_tick_frequency(1);

    if (movement_state.settings.bit.button_should_sound) {
        movement_play_note(movement_state.next_face_idx ? BUZZER_NOTE_C7 : BUZZER_NOTE_C8, 50);
    }

    wf->activate(watch_face_contexts[movement_state.current_face_idx]);

    movement_event_t event;
    event.subsecond = 0;
    event.event_type = EVENT_ACTIVATE;
    movement_state.watch_face_changed = false;
    wf->loop(event, watch_face_contexts[movement_state.current_face_idx]);

    movement_volatile_state.passthrough_events = _movement_button_events_mask;
}

// movement.c:1266-1412, without the accelerometer and the USB shell.
static void _app_loop(void) {
    const watch_face_t *wf = &watch_faces[movement_state.current_face_idx];

    uint32_t pending_events = movement_volatile_state.pending_events;
    movement_volatile_state.pending_events = 0;

    movement_event_t event;
    event.event_type = EVENT_NONE;
    event.subsecond = movement_volatile_state.subsecond;

    if (movement_volatile_state.turn_led_off) {
        if (!movement_volatile_state.light_button.is_down) {
            movement_volatile_state.turn_led_off = false;
            movement_force_led_off();
        }
    }

    _movement_handle_button_presses(pending_events);

    if ((pending_events & (1 << EVENT_TICK))
        && event.subsecond == 0
        && movement_state.has_scheduled_background_task) {
        _movement_handle_scheduled_tasks();
    }

    bool resign_timeout = (pending_events & (1 << EVENT_TIMEOUT)) != 0;
    if (resign_timeout) {
        pending_events &= ~(1 << EVENT_TIMEOUT);
    }

    uint32_t passthrough_pending_events = pending_events & movement_volatile_state.passthrough_events;
    pending_events = pending_events & ~movement_volatile_state.passthrough_events;

    movement_event_type_t event_type = 0;
    while (passthrough_pending_events) {
        uint8_t next_event = __builtin_ctz(passthrough_pending_events);
        event.event_type = event_type + next_event;
        movement_default_loop_handler(event);
        passthrough_pending_events = passthrough_pending_events >> (next_event + 1);
        event_type = event_type + next_event + 1;
    }

    event_type = 0;
    while (pending_events) {
        uint8_t next_event = __builtin_ctz(pending_events);
        event.event_type = event_type + next_event;
        wf->loop(event, watch_face_contexts[movement_state.current_face_idx]);
        pending_events = pending_events >> (next_event + 1);
        event_type = event_type + next_event + 1;
    }

    if (movement_volatile_state.minute_alarm_fired) {
        movement_volatile_state.minute_alarm_fired = false;
        _movement_renew_top_of_minute_alarm();
        _movement_handle_top_of_minute();
    }

    if (resign_timeout && movement_state.current_face_idx != 0) {
        event.event_type = EVENT_TIMEOUT;
        wf->loop(event, watch_face_contexts[movement_state.current_face_idx]);
    }

    if (movement_state.watch_face_changed) {
        _switch_face();
    }

    if (movement_volatile_state.enter_sleep_mode && !movement_volatile_state.is_buzzing) {
        movement_volatile_state.enter_sleep_mode = false;
        movement_volatile_state.is_sleeping = true;
        _movement_disable_inactivity_countdown();
        // The first pass runs at once; later ones run from _step.
        if (_sleep_mode_pass()) _wake_from_sleep_mode();
    }

    movement_volatile_state.schedule_next_comp = false;
}

// ---------------------------------------------------------------------------
// Interrupt callbacks: movement.c:1414-1561.

static movement_event_type_t _process_button_event(bool pin_level, movement_button_t *button) {
    movement_event_type_t event_type = EVENT_NONE;

    if (pin_level == button->is_down) {
        return event_type;
    }

    uint32_t counter = watch_rtc_get_counter();
    button->is_down = pin_level;

    if (pin_level) {
        button->down_timestamp = counter;
        event_type = button->down_event;
    } else {
        if ((counter - button->down_timestamp) >= MOVEMENT_REALLY_LONG_PRESS_TICKS) {
            event_type = button->down_event + 3;
        } else if ((counter - button->down_timestamp) >= MOVEMENT_LONG_PRESS_TICKS) {
            event_type = button->down_event + 3;
        } else {
            event_type = button->down_event + 1;
        }
    }

    return event_type;
}

static void cb_light_btn_interrupt(void) {
    bool pin_level = HAL_GPIO_BTN_LIGHT_read();
    movement_volatile_state.pending_events |= 1 << _process_button_event(pin_level, &movement_volatile_state.light_button);
}

static void cb_mode_btn_interrupt(void) {
    bool pin_level = HAL_GPIO_BTN_MODE_read();
    movement_volatile_state.pending_events |= 1 << _process_button_event(pin_level, &movement_volatile_state.mode_button);
}

static void cb_alarm_btn_interrupt(void) {
    bool pin_level = HAL_GPIO_BTN_ALARM_read();
    movement_volatile_state.pending_events |= 1 << _process_button_event(pin_level, &movement_volatile_state.alarm_button);
}

static movement_event_type_t _process_button_longpress_timeout(bool pin_level, movement_button_t *button) {
    if (!button->is_down) {
        return EVENT_NONE;
    }

    uint32_t counter = watch_rtc_get_counter();
    bool max_long_press = (counter - button->down_timestamp) >= MOVEMENT_MAX_LONG_PRESS_TICKS;
    bool really_long_press = (counter - button->down_timestamp) >= MOVEMENT_REALLY_LONG_PRESS_TICKS;

    if (pin_level) {
        if (max_long_press) {
            return EVENT_NONE;
        } else if (really_long_press) {
            return button->down_event + 4;
        } else {
            return button->down_event + 2;
        }
    } else {
        button->is_down = false;
        if (max_long_press || really_long_press) {
            return button->down_event + 3;
        } else {
            return button->down_event + 1;
        }
    }
}

static void cb_light_btn_timeout_interrupt(void) {
    bool pin_level = HAL_GPIO_BTN_LIGHT_read();
    movement_volatile_state.pending_events |= 1 << _process_button_longpress_timeout(pin_level, &movement_volatile_state.light_button);
}

static void cb_mode_btn_timeout_interrupt(void) {
    bool pin_level = HAL_GPIO_BTN_MODE_read();
    movement_volatile_state.pending_events |= 1 << _process_button_longpress_timeout(pin_level, &movement_volatile_state.mode_button);
}

static void cb_alarm_btn_timeout_interrupt(void) {
    bool pin_level = HAL_GPIO_BTN_ALARM_read();
    movement_volatile_state.pending_events |= 1 << _process_button_longpress_timeout(pin_level, &movement_volatile_state.alarm_button);
}

static void cb_led_timeout_interrupt(void) {
    movement_volatile_state.turn_led_off = true;
}

static void cb_resign_timeout_interrupt(void) {
    movement_volatile_state.pending_events |= 1 << EVENT_TIMEOUT;
}

static void cb_sleep_timeout_interrupt(void) {
    movement_request_sleep();
}

static void cb_alarm_btn_extwake(void) {
    movement_request_wake();
}

static void cb_minute_alarm_fired(void) {
    movement_volatile_state.minute_alarm_fired = true;
}

static void cb_tick(void) {
    rtc_counter_t counter = watch_rtc_get_counter();
    uint32_t freq = watch_rtc_get_frequency();
    uint32_t half_freq = freq >> 1;
    uint32_t subsecond_mask = freq - 1;
    movement_volatile_state.pending_events |= 1 << EVENT_TICK;
    movement_volatile_state.subsecond = ((counter + half_freq) & subsecond_mask) >> movement_state.tick_pern;
    tick_count++;
}

// ---------------------------------------------------------------------------
// The driver.

// One RTC count. The watch sleeps between interrupts, so app_loop (or, in
// low-energy mode, a pass of the sleep loop) runs only when one fired.
static void _step(void) {
    if (!hw_step()) return;
    if (movement_volatile_state.is_sleeping) {
        if (movement_volatile_state.minute_alarm_fired || movement_volatile_state.exit_sleep_mode) {
            if (_sleep_mode_pass()) _wake_from_sleep_mode();
        }
    } else {
        _app_loop();
    }
}

static void _require_face(const char *what) {
    if (watch_faces[HARNESS_FACE_INDEX].loop == NULL) {
        fprintf(stderr, "harness: %s before harness_load_face\n", what);
        abort();
    }
}

movement_settings_t harness_default_settings(void) {
    // movement.c:1005-1048 (app_init with no settings file)
    movement_settings_t settings = {0};
    settings.bit.version = 0;
    settings.bit.clock_mode_24h = MOVEMENT_DEFAULT_24H_MODE;
    settings.bit.time_zone = UTZ_UTC;
    settings.bit.led_red_color = MOVEMENT_DEFAULT_RED_COLOR;
    settings.bit.led_green_color = MOVEMENT_DEFAULT_GREEN_COLOR;
    settings.bit.led_blue_color = MOVEMENT_DEFAULT_BLUE_COLOR;
    settings.bit.use_imperial_units = MOVEMENT_DEFAULT_IMPERIAL_UNITS;
    settings.bit.button_should_sound = MOVEMENT_DEFAULT_BUTTON_SOUND;
    settings.bit.button_volume = MOVEMENT_DEFAULT_BUTTON_VOLUME;
    settings.bit.to_interval = MOVEMENT_DEFAULT_TIMEOUT_INTERVAL;
    settings.bit.le_interval = MOVEMENT_DEFAULT_LOW_ENERGY_INTERVAL;
    settings.bit.led_duration = MOVEMENT_DEFAULT_LED_DURATION;
    return settings;
}

void harness_init(watch_lcd_type_t lcd, watch_date_time_t utc, movement_settings_t settings) {
    for (uint8_t i = 0; i < MOVEMENT_NUM_FACES; i++) free(watch_face_contexts[i]);
    memset(watch_face_contexts, 0, sizeof(watch_face_contexts));
    memset(scheduled_tasks, 0, sizeof(scheduled_tasks));
    memset(watch_faces, 0, sizeof(watch_faces));
    memset(&movement_state, 0, sizeof(movement_state));
    memset(&movement_volatile_state, 0, sizeof(movement_volatile_state));
    memset(_movement_dst_offset_cache, 0, sizeof(_movement_dst_offset_cache));
    _pending_sequence = NULL;
    tick_count = 0;

    fake_lcd_reset(lcd);
    hw_reset(utc);
    watch_faces[0] = home_face;

    // movement.c:953-1076 (app_init), without the hardware and filesystem
    movement_state.settings = settings;
    movement_volatile_state.mode_button.down_event = EVENT_MODE_BUTTON_DOWN;
    movement_volatile_state.mode_button.timeout_index = MODE_BUTTON_TIMEOUT;
    movement_volatile_state.mode_button.cb_longpress = cb_mode_btn_timeout_interrupt;
    movement_volatile_state.light_button.down_event = EVENT_LIGHT_BUTTON_DOWN;
    movement_volatile_state.light_button.timeout_index = LIGHT_BUTTON_TIMEOUT;
    movement_volatile_state.light_button.cb_longpress = cb_light_btn_timeout_interrupt;
    movement_volatile_state.alarm_button.down_event = EVENT_ALARM_BUTTON_DOWN;
    movement_volatile_state.alarm_button.timeout_index = ALARM_BUTTON_TIMEOUT;
    movement_volatile_state.alarm_button.cb_longpress = cb_alarm_btn_timeout_interrupt;
    watch_buzzer_register_global_callbacks(cb_buzzer_start, cb_buzzer_stop);
    _movement_update_dst_offset_cache();
    movement_state.signal_volume = MOVEMENT_DEFAULT_SIGNAL_VOLUME;
    movement_state.alarm_volume = MOVEMENT_DEFAULT_ALARM_VOLUME;
    movement_state.next_available_backup_register = 2;
    _movement_reset_inactivity_countdown();
    _movement_set_top_of_minute_alarm();
}

void harness_load_face(const watch_face_t *face) {
    watch_faces[HARNESS_FACE_INDEX] = *face;
    movement_state.current_face_idx = HARNESS_FACE_INDEX;
    _app_setup();
    _app_loop();
}

void *harness_context(void) {
    return watch_face_contexts[HARNESS_FACE_INDEX];
}

uint8_t harness_current_face(void) {
    return (uint8_t)movement_state.current_face_idx;
}

void harness_button_down(harness_button_t button) {
    _require_face("harness_button_down");
    hw_set_button(button, true);
    if (movement_volatile_state.is_sleeping) {
        // Only ALARM wakes the watch (movement.c:1364), and that press is
        // consumed: the button interrupts aren't registered while asleep.
        if (button == BUTTON_ALARM) {
            cb_alarm_btn_extwake();
            if (_sleep_mode_pass()) _wake_from_sleep_mode();
        }
        return;
    }
    switch (button) {
        case BUTTON_LIGHT: cb_light_btn_interrupt(); break;
        case BUTTON_MODE: cb_mode_btn_interrupt(); break;
        case BUTTON_ALARM: cb_alarm_btn_interrupt(); break;
    }
    _app_loop();
}

void harness_button_up(harness_button_t button) {
    _require_face("harness_button_up");
    hw_set_button(button, false);
    if (movement_volatile_state.is_sleeping) return;
    switch (button) {
        case BUTTON_LIGHT: cb_light_btn_interrupt(); break;
        case BUTTON_MODE: cb_mode_btn_interrupt(); break;
        case BUTTON_ALARM: cb_alarm_btn_interrupt(); break;
    }
    _app_loop();
}

void harness_press(harness_button_t button, harness_press_t press) {
    harness_button_down(button);
    if (press == PRESS_LONG) {
        for (int i = 0; i < MOVEMENT_LONG_PRESS_TICKS; i++) _step();
    }
    harness_button_up(button);
}

void harness_tick(uint32_t n) {
    _require_face("harness_tick");
    uint32_t target = tick_count + n;
    while (tick_count != target) {
        if (movement_volatile_state.is_sleeping) {
            fprintf(stderr, "harness_tick: the watch is in low-energy mode, which has no ticks\n");
            abort();
        }
        _step();
    }
}

void harness_advance(uint32_t seconds) {
    _require_face("harness_advance");
    uint32_t steps = seconds * watch_rtc_get_frequency();
    for (uint32_t i = 0; i < steps; i++) _step();
}

void harness_set_time(watch_date_time_t utc) {
    movement_set_utc_timestamp(watch_utility_date_time_to_unix_time(utc, 0));
}

watch_date_time_t harness_utc_date_time(void) {
    return watch_rtc_get_date_time();
}

const char *harness_text(watch_position_t position) {
    static char buf[16];
    return fake_lcd_text(position, buf, sizeof(buf));
}

bool harness_indicator(watch_indicator_t indicator) {
    return fake_lcd_indicator(indicator);
}

uint8_t harness_tick_frequency(void) {
    return movement_state.tick_frequency;
}

bool harness_is_sleeping(void) {
    return movement_volatile_state.is_sleeping;
}

bool harness_background_task(watch_date_time_t *when) {
    if (scheduled_tasks[HARNESS_FACE_INDEX].reg == 0) return false;
    if (when) *when = scheduled_tasks[HARNESS_FACE_INDEX];
    return true;
}

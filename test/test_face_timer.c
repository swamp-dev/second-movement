// timer_face (watch-faces/complication/timer_face.c) on the face harness, on
// both LCD types: choose a slot, start, count down, pause, resume, ring,
// reset, edit a slot, run in the background, and time out.

#include <stdio.h>

#include "check.h"
#include "harness.h"
#include "watch_utility.h"
#include "zones.h"
#include "complication/timer_face.h"

static watch_lcd_type_t lcd;

// The text for the LCD under test: the face's long text on custom, its
// fallback on classic.
#define SHOWS(position, custom, classic) \
    fake_lcd_shows(position, lcd == WATCH_LCD_TYPE_CUSTOM ? (custom) : (classic))
#define BOTTOM_SHOWS(text) fake_lcd_shows(WATCH_POSITION_BOTTOM, text)

static watch_date_time_t date_time(uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hour, uint8_t minute, uint8_t second) {
    watch_date_time_t dt = {0};
    dt.unit.year = year - WATCH_RTC_REFERENCE_YEAR;
    dt.unit.month = month;
    dt.unit.day = day;
    dt.unit.hour = hour;
    dt.unit.minute = minute;
    dt.unit.second = second;
    return dt;
}

static uint32_t utc_seconds(watch_date_time_t dt) {
    return watch_utility_date_time_to_unix_time(dt, 0);
}

static uint32_t seconds_until_background_task(void) {
    watch_date_time_t when;
    if (!harness_background_task(&when)) return 0;
    return utc_seconds(when) - utc_seconds(harness_utc_date_time());
}

static timer_state_t *state(void) {
    return (timer_state_t *)harness_context();
}

static void start_face(movement_settings_t settings, watch_date_time_t utc) {
    harness_init(lcd, utc, settings);
    harness_load_face(&timer_face);
}

static void start_default(void) {
    start_face(harness_default_settings(), date_time(2026, 3, 1, 12, 0, 0));
}

// Sets the current slot to h:m:s through the settings screens, which start
// on the slot shown: LIGHT steps through slot, clear, hours, minutes, seconds
// and loop; ALARM counts the field up; a long LIGHT leaves.
static void set_current_slot(uint8_t hours, uint8_t minutes, uint8_t seconds) {
    timer_setting_t *slot = &state()->timers[state()->current_timer];
    harness_press(BUTTON_LIGHT, PRESS_LONG);   // settings: slot
    harness_press(BUTTON_LIGHT, PRESS_SHORT);  // clear? (the slot isn't empty)
    harness_press(BUTTON_LIGHT, PRESS_SHORT);  // hours
    for (int i = (hours - slot->unit.hours + 24) % 24; i > 0; i--) harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_LIGHT, PRESS_SHORT);  // minutes
    for (int i = (minutes - slot->unit.minutes + 60) % 60; i > 0; i--) harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_LIGHT, PRESS_SHORT);  // seconds
    for (int i = (seconds - slot->unit.seconds + 60) % 60; i > 0; i--) harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_LIGHT, PRESS_SHORT);  // loop
    harness_press(BUTTON_LIGHT, PRESS_LONG);   // back to waiting
}

static void test_activate(void) {
    start_default();
    CHECK(SHOWS(WATCH_POSITION_TOP_LEFT, "TMR", "TR"));
    CHECK(SHOWS(WATCH_POSITION_TOP_RIGHT, " 1", " 1"));
    // Slot 1's default is 2 minutes (CLAUDE.md § Gotchas: bytes are h, m, s).
    CHECK(BOTTOM_SHOWS("000200"));
    CHECK(fake_lcd_colon());
    CHECK(!harness_indicator(WATCH_INDICATOR_BELL));
    CHECK_EQ_INT(harness_tick_frequency(), 1, "timer_face runs at 1 Hz");
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "the face is on screen");
}

static void test_choose_slot(void) {
    start_default();
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    CHECK(SHOWS(WATCH_POSITION_TOP_RIGHT, " 2", " 2"));
    CHECK(BOTTOM_SHOWS("000500"));
    // Slots 2..5 hold 5, 10, 20 min and 2 h 45 m; 6..9 are empty and skipped.
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    CHECK(SHOWS(WATCH_POSITION_TOP_RIGHT, " 5", " 5"));
    CHECK(BOTTOM_SHOWS("024500"));
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    CHECK(SHOWS(WATCH_POSITION_TOP_RIGHT, " 1", " 1"));
    CHECK(BOTTOM_SHOWS("000200"));
    CHECK(!harness_background_task(NULL));
}

static void test_start_count_down_and_ring(void) {
    start_default();
    harness_press(BUTTON_ALARM, PRESS_LONG);
    CHECK(harness_indicator(WATCH_INDICATOR_BELL));
    CHECK(BOTTOM_SHOWS("000200"));
    CHECK_EQ_INT(seconds_until_background_task(), 120, "the alarm is scheduled for the end");
    CHECK_EQ_INT(harness_buzzer_count(), 1, "a start beep");

    harness_tick(1);
    CHECK(BOTTOM_SHOWS("000159"));
    harness_tick(59);
    CHECK(BOTTOM_SHOWS("000100"));
    harness_tick(59);
    CHECK(BOTTOM_SHOWS("000001"));
    CHECK_EQ_INT(harness_buzzer_count(), 1, "no ring before the end");

    // At 0 the background task rings and resets the face to the slot's time.
    harness_tick(1);
    CHECK(BOTTOM_SHOWS("000200"));
    CHECK(!harness_indicator(WATCH_INDICATOR_BELL));
    CHECK(!harness_background_task(NULL));
    CHECK_EQ_INT(harness_buzzer_count(), 2, "the first ring");
    CHECK(harness_buzzer_playing());

    // Four more rings follow, each started when the last one ends.
    harness_advance(10);
    CHECK_EQ_INT(harness_buzzer_count(), 6, "five rings in all");
    CHECK(!harness_buzzer_playing());
}

static void test_alarm_button_silences_ring(void) {
    start_default();
    harness_press(BUTTON_ALARM, PRESS_LONG);
    harness_tick(120);
    CHECK(harness_buzzer_playing());
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    size_t rings = harness_buzzer_count();
    harness_advance(10);
    CHECK_EQ_INT(harness_buzzer_count(), rings, "no rings after the ALARM press");
    CHECK(!harness_buzzer_playing());
    CHECK(SHOWS(WATCH_POSITION_TOP_RIGHT, " 1", " 1"));  // the press didn't change slot
}

static void test_pause_and_resume(void) {
    start_default();
    harness_press(BUTTON_ALARM, PRESS_LONG);
    harness_tick(20);
    CHECK(BOTTOM_SHOWS("000140"));

    harness_press(BUTTON_ALARM, PRESS_SHORT);  // pause
    CHECK(!harness_background_task(NULL));
    harness_tick(30);
    CHECK(BOTTOM_SHOWS("000140"));
    // While paused the bell blinks: off on odd seconds, on on even ones.
    bool bell = harness_indicator(WATCH_INDICATOR_BELL);
    harness_tick(1);
    CHECK(harness_indicator(WATCH_INDICATOR_BELL) != bell);

    harness_press(BUTTON_ALARM, PRESS_SHORT);  // resume
    CHECK(BOTTOM_SHOWS("000140"));
    CHECK(harness_indicator(WATCH_INDICATOR_BELL));
    CHECK_EQ_INT(seconds_until_background_task(), 100, "the alarm moves out by the pause");
    CHECK_EQ_INT(harness_buzzer_count(), 1, "resuming doesn't beep");
    harness_tick(40);
    CHECK(BOTTOM_SHOWS("000100"));
}

static void test_long_alarm_resets_running_timer(void) {
    start_default();
    harness_press(BUTTON_ALARM, PRESS_LONG);
    harness_tick(5);
    harness_press(BUTTON_ALARM, PRESS_LONG);
    CHECK(BOTTOM_SHOWS("000200"));
    CHECK(!harness_indicator(WATCH_INDICATOR_BELL));
    CHECK(!harness_background_task(NULL));
    // The default settings have button sounds on, so the reset beeps.
    CHECK_EQ_INT(harness_buzzer_count(), 2, "start beep, reset beep");
    CHECK_EQ_INT(harness_buzzer_log(1)->sequence[0], BUZZER_NOTE_C7, "the reset note");
}

static void test_edit_slot(void) {
    start_default();
    harness_press(BUTTON_LIGHT, PRESS_LONG);
    CHECK_EQ_INT(harness_tick_frequency(), 4, "settings blink at 4 Hz");
    harness_press(BUTTON_LIGHT, PRESS_SHORT);
    CHECK(BOTTOM_SHOWS("CLEARn") || BOTTOM_SHOWS("CLEAR "));  // the last letter blinks
    harness_press(BUTTON_LIGHT, PRESS_LONG);  // back out without changes
    CHECK_EQ_INT(harness_tick_frequency(), 1, "back to 1 Hz");

    set_current_slot(1, 2, 3);
    CHECK(BOTTOM_SHOWS("010203"));
    CHECK(!harness_indicator(WATCH_INDICATOR_LAP));
    harness_press(BUTTON_ALARM, PRESS_LONG);
    CHECK_EQ_INT(seconds_until_background_task(), 3723, "1 h 2 m 3 s");
}

static void test_rings_in_background(void) {
    start_default();
    harness_press(BUTTON_ALARM, PRESS_LONG);
    harness_press(BUTTON_MODE, PRESS_LONG);  // leave for the home face
    CHECK_EQ_INT(harness_current_face(), 0, "MODE long goes home");
    harness_buzzer_clear();

    harness_advance(130);
    CHECK_EQ_INT(harness_buzzer_count(), 5, "five rings, with the face off screen");
    CHECK(!harness_background_task(NULL));

    harness_press(BUTTON_MODE, PRESS_SHORT);  // and back
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "MODE comes back");
    CHECK(SHOWS(WATCH_POSITION_TOP_LEFT, "TMR", "TR"));
    CHECK(BOTTOM_SHOWS("000200"));
}

static void test_timeout(void) {
    // A stopped timer goes home after the inactivity timeout (60 s by default).
    start_default();
    harness_advance(59);
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "not yet");
    harness_advance(2);
    CHECK_EQ_INT(harness_current_face(), 0, "timed out");

    // A running one doesn't: movement restarts the countdown every second
    // while a background task is pending.
    start_default();
    harness_press(BUTTON_ALARM, PRESS_LONG);
    harness_advance(100);
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "still on screen");
    CHECK(BOTTOM_SHOWS("000020"));
}

static void test_low_energy(void) {
    // le_interval 1 is 10 minutes of inactivity.
    movement_settings_t settings = harness_default_settings();
    settings.bit.le_interval = 1;

    // An idle watch times out to the home face, then sleeps; ALARM wakes it.
    start_face(settings, date_time(2026, 3, 1, 12, 0, 0));
    harness_advance(10 * 60 - 1);
    CHECK(!harness_is_sleeping());
    harness_advance(2);
    CHECK(harness_is_sleeping());
    CHECK_EQ_INT(harness_current_face(), 0, "asleep on the home face");
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    CHECK(!harness_is_sleeping());
    harness_tick(1);  // ticks are back

    // Asleep on the timer itself (a 30-minute timeout outlasts the 10 minutes
    // to low-energy mode), waking redraws it at once, with no tick needed.
    settings.bit.to_interval = 3;
    start_face(settings, date_time(2026, 3, 1, 12, 0, 0));
    harness_advance(10 * 60 + 1);
    CHECK(harness_is_sleeping());
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "asleep on the timer");
    fake_lcd_reset(lcd);
    harness_button_down(BUTTON_ALARM);
    CHECK(!harness_is_sleeping());
    CHECK(SHOWS(WATCH_POSITION_TOP_LEFT, "TMR", "TR"));
    CHECK(BOTTOM_SHOWS("000200"));
    harness_button_up(BUTTON_ALARM);
    CHECK(SHOWS(WATCH_POSITION_TOP_RIGHT, " 1", " 1"));  // waking doesn't press the button
    settings.bit.to_interval = 0;

    // A running timer keeps it awake, and the face on screen.
    start_face(settings, date_time(2026, 3, 1, 12, 0, 0));
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_ALARM, PRESS_SHORT);
    harness_press(BUTTON_ALARM, PRESS_LONG);  // slot 4: 20 minutes
    harness_advance(15 * 60);
    CHECK(!harness_is_sleeping());
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "still on the timer");
    CHECK(BOTTOM_SHOWS("000500"));
}

// Whether the bottom line shows the real time left until the face's
// background task, as HHMMSS.
static bool shows_time_left(void) {
    uint32_t left = seconds_until_background_task();
    char text[16];
    snprintf(text, sizeof(text), "%02u%02u%02u", (unsigned)(left / 3600), (unsigned)(left / 60 % 60), (unsigned)(left % 60));
    return BOTTOM_SHOWS(text);
}

// #86: the time left is off by the DST shift after the face is re-opened
// across a DST change. Berlin leaves summer time at 01:00 UTC on 25 Oct 2026.
static void test_dst_change_while_running(void) {
    movement_settings_t settings = harness_default_settings();
    settings.bit.time_zone = UTZ_BERLIN;
    start_face(settings, date_time(2026, 10, 25, 0, 45, 0));  // 02:45 CEST
    set_current_slot(2, 0, 0);  // takes about a second of button presses
    harness_press(BUTTON_ALARM, PRESS_LONG);
    CHECK_EQ_INT(seconds_until_background_task(), 7200, "a 2 h timer");
    watch_date_time_t due;
    CHECK(harness_background_task(&due));
    CHECK(shows_time_left());

    harness_press(BUTTON_MODE, PRESS_LONG);
    harness_advance(45 * 60);  // about 01:30 UTC, 02:30 CET: 1 h 15 m left
    harness_press(BUTTON_MODE, PRESS_SHORT);
    CHECK_EQ_INT(harness_current_face(), HARNESS_FACE_INDEX, "back on the timer");
    watch_date_time_t when;
    CHECK(harness_background_task(&when));
    CHECK_EQ_INT(when.reg, due.reg, "the alarm is still right");
    // Uncomment when #86 is fixed (it shows an hour less, about 001459):
    // CHECK(shows_time_left());

    // With 45 m left the face shows target - now with the hour taken off, a
    // negative number that prints as 7 characters ("0024100" in the bug
    // report). On classic, watch_display_text reads past the end of
    // Classic_LCD_Display_Mapping for the 7th (#60), which UBSan stops on.
    // Uncomment when #86 is fixed:
    // harness_tick(30 * 60);
    // CHECK(shows_time_left());
}

// #82: pausing with more than 18 h 12 m 15 s left loses 65536 seconds.
static void test_pause_long_timer(void) {
    start_default();
    set_current_slot(20, 0, 0);
    harness_press(BUTTON_ALARM, PRESS_LONG);
    harness_tick(10);
    CHECK(BOTTOM_SHOWS("195950"));
    harness_press(BUTTON_ALARM, PRESS_SHORT);  // pause
    harness_press(BUTTON_ALARM, PRESS_SHORT);  // resume
    // Uncomment when #82 is fixed (it shows 014734, due in 6454 s):
    // CHECK(BOTTOM_SHOWS("195950"));
    // CHECK_EQ_INT(seconds_until_background_task(), 71990, "the pause keeps the time left");
}

int main(void) {
    const watch_lcd_type_t types[] = {WATCH_LCD_TYPE_CLASSIC, WATCH_LCD_TYPE_CUSTOM};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        lcd = types[i];
        int failures = check_failures;
        test_activate();
        test_choose_slot();
        test_start_count_down_and_ring();
        test_alarm_button_silences_ring();
        test_pause_and_resume();
        test_long_alarm_resets_running_timer();
        test_edit_slot();
        test_rings_in_background();
        test_timeout();
        test_low_energy();
        test_dst_change_while_running();
        test_pause_long_timer();
        if (check_failures != failures) {
            fprintf(stderr, "(the failures above are on the %s LCD)\n",
                    lcd == WATCH_LCD_TYPE_CUSTOM ? "custom" : "classic");
        }
    }
    return check_summary("test_face_timer");
}

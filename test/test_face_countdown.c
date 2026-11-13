// countdown_face (watch-faces/complication/countdown_face.c) on the face
// harness, on both LCD types: activate, then one countdown to the alarm.
// It also shows what a test for another face takes: this file plus its two
// lines in test/Makefile.

#include "check.h"
#include "harness.h"
#include "complication/countdown_face.h"

static void test_countdown(watch_lcd_type_t lcd) {
    watch_date_time_t utc = {0};
    utc.unit.year = 2026 - WATCH_RTC_REFERENCE_YEAR;
    utc.unit.month = 3;
    utc.unit.day = 1;
    utc.unit.hour = 12;
    harness_init(lcd, utc, harness_default_settings());
    harness_load_face(&countdown_face);

    CHECK(fake_lcd_shows(WATCH_POSITION_TOP, lcd == WATCH_LCD_TYPE_CUSTOM ? "TIMER" : "CD"));
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, " 00300"));  // the default 3 minutes

    harness_press(BUTTON_ALARM, PRESS_SHORT);  // start
    // Uncomment when #119 is fixed (draw() clears SIGNAL straight away):
    // CHECK(harness_indicator(WATCH_INDICATOR_SIGNAL));
    CHECK(harness_background_task(NULL));
    harness_tick(1);
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, " 00259"));
    harness_tick(178);
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, " 00001"));

    harness_buzzer_clear();
    harness_tick(1);  // the alarm plays and the countdown resets
    CHECK_EQ_INT(harness_buzzer_count(), 1, "the alarm");
    CHECK_EQ_INT(harness_buzzer_log(0)->sequence[0], BUZZER_NOTE_C8, "movement's alarm tune");
    CHECK_EQ_INT(harness_buzzer_log(0)->volume, WATCH_BUZZER_VOLUME_LOUD, "at the alarm volume");
    CHECK(fake_lcd_shows(WATCH_POSITION_BOTTOM, " 00300"));
    CHECK(!harness_indicator(WATCH_INDICATOR_SIGNAL));
    CHECK(!harness_background_task(NULL));
}

int main(void) {
    test_countdown(WATCH_LCD_TYPE_CLASSIC);
    test_countdown(WATCH_LCD_TYPE_CUSTOM);
    return check_summary("test_face_countdown");
}

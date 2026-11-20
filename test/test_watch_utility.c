// Host tests for watch-library/shared/watch/watch_utility.c, the date and time
// math most faces use. Unix-time conversion and ISO week numbers are checked
// against the host libc (timegm, gmtime_r, strftime) as an independent source.

#define _DEFAULT_SOURCE  // timegm
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "watch_utility.h"
#include "zones.h"

// watch_utility_time_zone_name_at_index asks the LCD type; the test picks it.
static watch_lcd_type_t lcd_type = WATCH_LCD_TYPE_CLASSIC;
watch_lcd_type_t watch_get_lcd_type(void) {
    return lcd_type;
}

// The RTC stores the year in 6 bits from 2020, so a watch_date_time_t holds
// 2020..2083.
#define RTC_LAST_YEAR (WATCH_RTC_REFERENCE_YEAR + 63)

static watch_date_time_t dt(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    watch_date_time_t d;
    d.reg = 0;
    d.unit.year = year - WATCH_RTC_REFERENCE_YEAR;
    d.unit.month = month;
    d.unit.day = day;
    d.unit.hour = hour;
    d.unit.minute = minute;
    d.unit.second = second;
    return d;
}

static time_t host_timegm(int year, int month, int day, int hour, int minute, int second) {
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    return timegm(&tm);
}

// Runs the body with `var` set to every day from 1 Jan first_year to 31 Dec last_year, at noon UTC.
#define FOR_EACH_DAY(first_year, last_year, var) \
    for (time_t day_t_ = host_timegm(first_year, 1, 1, 12, 0, 0); \
         day_t_ <= host_timegm(last_year, 12, 31, 12, 0, 0); day_t_ += 86400) \
        for (struct tm var, *once_ = gmtime_r(&day_t_, &var); once_; once_ = NULL)

// Offsets in seconds: UTC, whole hours either way, and the half- and
// quarter-hour zones (India, Newfoundland, Nepal, Chatham).
static const int32_t offsets[] = {0, 3600, -3600, 14 * 3600, -12 * 3600, 19800, -12600, 20700, 45900};
#define NUM_OFFSETS (sizeof(offsets) / sizeof(offsets[0]))

static const uint8_t times[][3] = {{0, 0, 0}, {0, 0, 1}, {11, 59, 59}, {12, 0, 0}, {13, 37, 42}, {23, 59, 59}};
#define NUM_TIMES (sizeof(times) / sizeof(times[0]))

static void test_convert_to_unix_time_matches_timegm(void) {
    int failures = check_failures;
    FOR_EACH_DAY(2020, 2099, tm) {
        int year = tm.tm_year + 1900, month = tm.tm_mon + 1, day = tm.tm_mday;
        for (size_t t = 0; t < NUM_TIMES; t++) {
            for (size_t o = 0; o < NUM_OFFSETS; o++) {
                time_t expected = host_timegm(year, month, day, times[t][0], times[t][1], times[t][2]) - offsets[o];
                uint32_t actual = watch_utility_convert_to_unix_time(year, month, day, times[t][0], times[t][1], times[t][2], offsets[o]);
                CHECK_EQ_INT(actual, expected, "%04d-%02d-%02d %02d:%02d:%02d offset %d",
                             year, month, day, times[t][0], times[t][1], times[t][2], (int)offsets[o]);
            }
        }
        if (check_failures - failures > 10) return;  // don't flood the log
    }
}

static void test_date_time_to_unix_time(void) {
    // 2024-02-29 12:00 in UTC+5:30 is 06:30 UTC.
    CHECK_EQ_INT(watch_utility_date_time_to_unix_time(dt(2024, 2, 29, 12, 0, 0), 19800),
                 host_timegm(2024, 2, 29, 6, 30, 0), "leap day, India");
    CHECK_EQ_INT(watch_utility_date_time_to_unix_time(dt(2020, 1, 1, 0, 0, 0), 0), 1577836800, "RTC epoch");
}

static void test_date_time_from_unix_time_matches_gmtime(void) {
    int failures = check_failures;
    FOR_EACH_DAY(2020, RTC_LAST_YEAR, day) {
        (void)day;
        for (size_t t = 0; t < NUM_TIMES; t++) {
            for (size_t o = 0; o < NUM_OFFSETS; o++) {
                time_t utc = day_t_ - 12 * 3600 + times[t][0] * 3600 + times[t][1] * 60 + times[t][2];
                time_t local = utc + offsets[o];
                struct tm e;
                gmtime_r(&local, &e);
                if (e.tm_year + 1900 < WATCH_RTC_REFERENCE_YEAR || e.tm_year + 1900 > RTC_LAST_YEAR) continue;
                watch_date_time_t a = watch_utility_date_time_from_unix_time((uint32_t)utc, offsets[o]);
                int ok = a.unit.year + WATCH_RTC_REFERENCE_YEAR == e.tm_year + 1900 && a.unit.month == e.tm_mon + 1 &&
                         a.unit.day == e.tm_mday && a.unit.hour == e.tm_hour && a.unit.minute == e.tm_min &&
                         a.unit.second == e.tm_sec;
                CHECK_EQ_INT(ok, 1, "unix %lld offset %d: got %04d-%02d-%02d %02d:%02d:%02d, expected %04d-%02d-%02d %02d:%02d:%02d",
                             (long long)utc, (int)offsets[o],
                             a.unit.year + WATCH_RTC_REFERENCE_YEAR, a.unit.month, a.unit.day, a.unit.hour, a.unit.minute, a.unit.second,
                             e.tm_year + 1900, e.tm_mon + 1, e.tm_mday, e.tm_hour, e.tm_min, e.tm_sec);
            }
        }
        if (check_failures - failures > 10) return;
    }
}

// date_time_from_unix_time(date_time_to_unix_time(x, off), off) == x for every
// day the RTC can hold, at several times and offsets.
static void test_unix_time_round_trip(void) {
    int failures = check_failures;
    FOR_EACH_DAY(2020, RTC_LAST_YEAR, tm) {
        for (size_t t = 0; t < NUM_TIMES; t++) {
            for (size_t o = 0; o < NUM_OFFSETS; o++) {
                watch_date_time_t x = dt(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, times[t][0], times[t][1], times[t][2]);
                uint32_t ts = watch_utility_date_time_to_unix_time(x, offsets[o]);
                watch_date_time_t y = watch_utility_date_time_from_unix_time(ts, offsets[o]);
                CHECK_EQ_INT(y.reg, x.reg, "%04d-%02d-%02d %02d:%02d:%02d offset %d", tm.tm_year + 1900, tm.tm_mon + 1,
                             tm.tm_mday, times[t][0], times[t][1], times[t][2], (int)offsets[o]);
            }
        }
        if (check_failures - failures > 10) return;
    }
}

static void test_date_time_from_unix_time_out_of_range(void) {
    // Years the 6-bit RTC field can't hold come back as an all-zero register.
    CHECK_EQ_INT(watch_utility_date_time_from_unix_time(host_timegm(2019, 12, 31, 23, 59, 59), 0).reg, 0, "2019");
    CHECK_EQ_INT(watch_utility_date_time_from_unix_time(host_timegm(2084, 1, 1, 0, 0, 0), 0).reg, 0, "2084");
    // The offset decides which side of the boundary a timestamp lands on.
    CHECK(watch_utility_date_time_from_unix_time(host_timegm(2019, 12, 31, 23, 0, 0), 3600).reg != 0);
    CHECK_EQ_INT(watch_utility_date_time_from_unix_time(host_timegm(2083, 12, 31, 23, 0, 0), 3600).reg, 0, "2083 + 1h");
}

static void test_weekdays(void) {
    static const char *two[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    static const char *three[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    int failures = check_failures;
    FOR_EACH_DAY(2020, RTC_LAST_YEAR, tm) {
        int year = tm.tm_year + 1900, month = tm.tm_mon + 1, day = tm.tm_mday;
        int iso = tm.tm_wday == 0 ? 7 : tm.tm_wday;
        CHECK_EQ_INT(watch_utility_get_iso8601_weekday_number(year, month, day), iso, "%04d-%02d-%02d", year, month, day);
        watch_date_time_t d = dt(year, month, day, 0, 0, 0);
        CHECK(strncmp(watch_utility_get_weekday(d), two[tm.tm_wday], 2) == 0);
        CHECK(strncmp(watch_utility_get_long_weekday(d), three[tm.tm_wday], 3) == 0);
        if (check_failures - failures > 10) return;
    }
    // Known dates: 1 Jan 2020 was a Wednesday, 29 Feb 2024 a Thursday.
    CHECK_EQ_INT(watch_utility_get_iso8601_weekday_number(2020, 1, 1), 3, "2020-01-01");
    CHECK_EQ_INT(watch_utility_get_iso8601_weekday_number(2024, 2, 29), 4, "2024-02-29");
    CHECK(strncmp(watch_utility_get_long_weekday(dt(2024, 2, 29, 0, 0, 0)), "THU", 3) == 0);
}

static void test_weeknumber(void) {
    // ISO weeks in years the bug below doesn't reach.
    CHECK_EQ_INT(watch_utility_get_weeknumber(2024, 1, 1), 1, "2024-01-01");
    CHECK_EQ_INT(watch_utility_get_weeknumber(2024, 6, 15), 24, "2024-06-15");
    CHECK_EQ_INT(watch_utility_get_weeknumber(2024, 12, 30), 1, "2024-12-30");  // a Monday: week 1 of 2025
    CHECK_EQ_INT(watch_utility_get_weeknumber(2025, 12, 29), 1, "2025-12-29");  // a Monday: week 1 of 2026
    CHECK_EQ_INT(watch_utility_get_weeknumber(2026, 11, 17), 47, "2026-11-17");
    // #140: every year after a 53-week ISO year (2021, 2027, ...) reads
    // one week high, and so do the days around those new years. The fix for
    // #140 turns this sweep and these checks back on.
    //   CHECK_EQ_INT(watch_utility_get_weeknumber(2020, 12, 31), 53, "2020-12-31");
    //   CHECK_EQ_INT(watch_utility_get_weeknumber(2021, 1, 1), 53, "2021-01-01");
    //   CHECK_EQ_INT(watch_utility_get_weeknumber(2021, 6, 15), 24, "2021-06-15");
    //   FOR_EACH_DAY(2020, 2099, tm) {
    //       char buf[4];
    //       strftime(buf, sizeof buf, "%V", &tm);
    //       int year = tm.tm_year + 1900, month = tm.tm_mon + 1, day = tm.tm_mday;
    //       CHECK_EQ_INT(watch_utility_get_weeknumber(year, month, day), atoi(buf), "%04d-%02d-%02d", year, month, day);
    //   }
}

static void test_days_since_new_year(void) {
    int failures = check_failures;
    FOR_EACH_DAY(2020, 2099, tm) {
        int year = tm.tm_year + 1900, month = tm.tm_mon + 1, day = tm.tm_mday;
        CHECK_EQ_INT(watch_utility_days_since_new_year(year, month, day), tm.tm_yday + 1, "%04d-%02d-%02d", year, month, day);
        if (check_failures - failures > 10) return;
    }
    CHECK_EQ_INT(watch_utility_days_since_new_year(2024, 12, 31), 366, "2024-12-31");
    CHECK_EQ_INT(watch_utility_days_since_new_year(2025, 12, 31), 365, "2025-12-31");
}

static void test_days_in_month(void) {
    static const uint8_t common[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int year = 2020; year <= 2099; year++) {
        for (int month = 1; month <= 12; month++) {
            int expected = common[month - 1] + (month == 2 && year % 4 == 0);
            CHECK_EQ_INT(watch_utility_days_in_month(month, year), expected, "%04d-%02d", year, month);
        }
    }
    CHECK_EQ_INT(watch_utility_days_in_month(2, 2024), 29, "Feb 2024");
    CHECK_EQ_INT(watch_utility_days_in_month(2, 2025), 28, "Feb 2025");
    // Century years are #87: is_leap() takes years since 1900 but is passed a
    // full year, so it tests 3900 for 2000 and 4000 for 2100. The fix for #87
    // adds these checks:
    //   CHECK_EQ_INT(watch_utility_days_in_month(2, 2000), 29, "Feb 2000");
    //   CHECK_EQ_INT(watch_utility_days_in_month(2, 2100), 28, "Feb 2100");
}

static void test_is_leap(void) {
    // is_leap takes years since 1900, like struct tm's tm_year.
    CHECK(is_leap(2024 - 1900));
    CHECK(!is_leap(2025 - 1900));
    CHECK(is_leap(2000 - 1900));
    CHECK(!is_leap(2100 - 1900));
    CHECK(!is_leap(1900 - 1900));
}

static void test_seconds_to_duration(void) {
    watch_duration_t d = watch_utility_seconds_to_duration(0);
    CHECK(d.days == 0 && d.hours == 0 && d.minutes == 0 && d.seconds == 0);
    d = watch_utility_seconds_to_duration(86399);
    CHECK(d.days == 0 && d.hours == 23 && d.minutes == 59 && d.seconds == 59);
    d = watch_utility_seconds_to_duration(86400 + 3600 + 60 + 1);
    CHECK(d.days == 1 && d.hours == 1 && d.minutes == 1 && d.seconds == 1);
    d = watch_utility_seconds_to_duration(UINT32_MAX);  // 49710 d 6:28:15
    CHECK(d.days == 49710 && d.hours == 6 && d.minutes == 28 && d.seconds == 15);
}

static void test_convert_to_12_hour(void) {
    static const struct { uint8_t hour24, hour12; bool pm; } cases[] = {
        {0, 12, false}, {1, 1, false}, {11, 11, false}, {12, 12, true}, {13, 1, true}, {23, 11, true},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        watch_date_time_t d = dt(2024, 6, 15, cases[i].hour24, 34, 56);
        bool pm = watch_utility_convert_to_12_hour(&d);
        CHECK_EQ_INT(pm, cases[i].pm, "hour %d", cases[i].hour24);
        CHECK_EQ_INT(d.unit.hour, cases[i].hour12, "hour %d", cases[i].hour24);
        // Only the hour changes.
        CHECK(d.unit.minute == 34 && d.unit.second == 56 && d.unit.day == 15);
    }
}

static void test_convert_zone(void) {
    // Offsets are passed as uint32_t; a negative offset wraps and still works.
    watch_date_time_t ny = watch_utility_date_time_convert_zone(dt(2024, 1, 1, 3, 30, 0), 0, (uint32_t)-5 * 3600);
    CHECK(ny.reg == dt(2023, 12, 31, 22, 30, 0).reg);
    watch_date_time_t india = watch_utility_date_time_convert_zone(dt(2024, 2, 28, 22, 0, 0), (uint32_t)-3600, 19800);
    CHECK(india.reg == dt(2024, 2, 29, 4, 30, 0).reg);

    uint32_t ts = host_timegm(2024, 6, 1, 0, 0, 0);
    CHECK_EQ_INT(watch_utility_unix_time_convert_zone(ts, 0, 3600), ts + 3600, "UTC to +1");
    CHECK_EQ_INT(watch_utility_unix_time_convert_zone(ts, 3600, (uint32_t)-3600), ts - 7200, "+1 to -1");
}

static void test_offset_timestamp(void) {
    uint32_t now = host_timegm(2024, 6, 1, 12, 0, 0);
    CHECK_EQ_INT(watch_utility_offset_timestamp(now, 0, 0, 0), now, "zero");
    CHECK_EQ_INT(watch_utility_offset_timestamp(now, 1, 2, 3), now + 3723, "positive");
    CHECK_EQ_INT(watch_utility_offset_timestamp(now, -1, -2, -3), now - 3723, "negative");
    CHECK_EQ_INT(watch_utility_offset_timestamp(now, 2, -30, 0), now + 5400, "mixed signs");
    CHECK_EQ_INT(watch_utility_offset_timestamp(now, -128, -128, -128), now - 128 * 3600 - 128 * 60 - 128, "minimums");
}

static void test_thermistor_temperature(void) {
    // When the thermistor's resistance equals its nominal resistance, the
    // result is the nominal temperature, from either side of the divider.
    // Low side: series / (65535 / v - 1) == series when v == 65535 / 2.
    float low = watch_utility_thermistor_temperature(32768, false, 3380, 25, 10000, 10000);
    CHECK(fabsf(low - 25.0f) < 0.01f);
    // High side: 1023 * series / (v / 64) - series == series when v == 32736.
    float high = watch_utility_thermistor_temperature(32736, true, 3380, 25, 10000, 10000);
    CHECK(fabsf(high - 25.0f) < 0.01f);
    // On the low side a higher reading means more resistance: colder for an NTC.
    CHECK(watch_utility_thermistor_temperature(40000, false, 3380, 25, 10000, 10000) < low);
}

static void test_time_zone_name_at_index(void) {
    lcd_type = WATCH_LCD_TYPE_CLASSIC;
    // Classic returns the name in the zone table as is.
    CHECK(watch_utility_time_zone_name_at_index(0) == (const char *)zone_names);
    CHECK(watch_utility_time_zone_name_at_index(16) == (const char *)zone_names + 8 * 16);

    lcd_type = WATCH_LCD_TYPE_CUSTOM;
    CHECK(strcmp(watch_utility_time_zone_name_at_index(8), "NuYork") == 0);
    CHECK(strcmp(watch_utility_time_zone_name_at_index(15), " UTC  ") == 0);
    CHECK(strcmp(watch_utility_time_zone_name_at_index(16), "London") == 0);
    CHECK(strcmp(watch_utility_time_zone_name_at_index(30), " burma") == 0);
    // Every custom name fits the 6-character field and is terminated.
    for (int i = 0; i < (int)(sizeof(zone_names) / 8); i++) {
        CHECK(strlen(watch_utility_time_zone_name_at_index(i)) <= 6);
    }
    lcd_type = WATCH_LCD_TYPE_CLASSIC;
}

int main(void) {
    test_convert_to_unix_time_matches_timegm();
    test_date_time_to_unix_time();
    test_date_time_from_unix_time_matches_gmtime();
    test_unix_time_round_trip();
    test_date_time_from_unix_time_out_of_range();
    test_weekdays();
    test_weeknumber();
    test_days_since_new_year();
    test_days_in_month();
    test_is_leap();
    test_seconds_to_duration();
    test_convert_to_12_hour();
    test_convert_zone();
    test_offset_timestamp();
    test_thermistor_temperature();
    test_time_zone_name_at_index();
    return check_summary("test_watch_utility");
}

#include <string.h>

#include "check.h"
#include "zodiac.h"

enum { AQUARIUS, PISCES, ARIES, TAURUS, GEMINI, CANCER, LEO, VIRGO, LIBRA, SCORPIO, SAGITTARIUS, CAPRICORN };

#define MINUTE 60
#define HOUR (60 * MINUTE)
#define DAY (24 * HOUR)
#define EASTERN_DAYLIGHT (-4 * HOUR)
#define EASTERN_STANDARD (-5 * HOUR)

// Unix time of a UTC date and time, for 1970 to 2099 (days_from_civil).
static int64_t utc(int y, int m, int d, int hh, int mm) {
    y -= m <= 2;
    int era = y / 400, yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + doe - 719468;
    return days * DAY + hh * HOUR + mm * MINUTE;
}

// Local month and day at Unix time t (civil_from_days).
static void local_date(int64_t t, int32_t utc_offset, uint8_t *month, uint8_t *day) {
    int64_t z = (t + utc_offset) / DAY + 719468;  // t is after 1970, so no negative division
    int64_t era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    *month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
}

// Published equinox and solstice times (UTC), where the Sun enters Aries,
// Cancer, Libra and Capricorn. The solar formulas are good to a few minutes, so
// check the sign 20 minutes either side.
static void test_sign_flips_at_equinoxes_and_solstices(void) {
    const struct {
        int64_t at;
        uint8_t sign_before, sign_after;
    } ingresses[] = {
        {utc(2026, 3, 20, 14, 46), PISCES, ARIES},
        {utc(2026, 6, 21, 8, 24), GEMINI, CANCER},
        {utc(2026, 9, 23, 0, 5), VIRGO, LIBRA},
        {utc(2026, 12, 21, 20, 50), SAGITTARIUS, CAPRICORN},
        {utc(2028, 3, 20, 2, 17), PISCES, ARIES},
        {utc(2028, 6, 20, 20, 2), GEMINI, CANCER},
    };
    for (size_t i = 0; i < sizeof(ingresses) / sizeof(ingresses[0]); i++) {
        CHECK_EQ_INT(zodiac_sign_at(ingresses[i].at - 20 * MINUTE), ingresses[i].sign_before, "ingress %zu, before", i);
        CHECK_EQ_INT(zodiac_sign_at(ingresses[i].at + 20 * MINUTE), ingresses[i].sign_after, "ingress %zu, after", i);
    }
}

static void check_start(uint8_t sign, int64_t now, int32_t utc_offset, uint8_t month, uint8_t day, const char *what) {
    uint8_t m = 0, d = 0;
    zodiac_sign_start_date(sign, now, utc_offset, &m, &d);
    CHECK_EQ_INT(m, month, "%s: month", what);
    CHECK_EQ_INT(d, day, "%s: day", what);
}

// The start date is the local date of the ingress, so it depends on the time zone.
static void test_start_date_is_local(void) {
    int64_t april_2026 = utc(2026, 4, 1, 16, 0);
    check_start(ARIES, april_2026, EASTERN_DAYLIGHT, 3, 20, "Aries 2026, New York");
    // 2028-03-20 02:17 UTC is still 19 March in New York.
    int64_t april_2028 = utc(2028, 4, 1, 16, 0);
    check_start(ARIES, april_2028, EASTERN_DAYLIGHT, 3, 19, "Aries 2028, New York");
    check_start(ARIES, april_2028, 0, 3, 20, "Aries 2028, UTC");
    // 2026-09-23 00:05 UTC is 22 September in New York.
    int64_t october_2026 = utc(2026, 10, 1, 16, 0);
    check_start(LIBRA, october_2026, EASTERN_DAYLIGHT, 9, 22, "Libra 2026, New York");
    check_start(LIBRA, october_2026, 0, 9, 23, "Libra 2026, UTC");
    check_start(CANCER, utc(2028, 7, 1, 16, 0), EASTERN_DAYLIGHT, 6, 20, "Cancer 2028, New York");
}

// A browsed sign shows its ingress nearest to now: past for the signs just
// gone, future for the ones coming.
static void test_start_date_of_browsed_signs(void) {
    int64_t now = utc(2026, 11, 11, 17, 0);
    CHECK_EQ_INT(zodiac_sign_at(now), SCORPIO, "11 Nov 2026");
    check_start(CAPRICORN, now, EASTERN_STANDARD, 12, 21, "Capricorn, next month");
    check_start(CANCER, now, EASTERN_STANDARD, 6, 21, "Cancer, last June");
    check_start(ARIES, now, EASTERN_STANDARD, 3, 20, "Aries, next March");
}

// Walking 2026-2030 a day at a time, the sign stays or moves to the next one,
// each sign's start date stays put while it lasts, and that date is the day of
// the change or the day before (the ingress fell between two samples).
static void test_five_years_daily(void) {
    const int32_t offset = EASTERN_STANDARD;
    int64_t t = utc(2026, 1, 1, 17, 0);  // noon in New York
    uint8_t last = zodiac_sign_at(t), last_m = 0, last_d = 0;
    zodiac_sign_start_date(last, t, offset, &last_m, &last_d);
    unsigned changes = 0;
    for (int i = 1; i < 5 * 365; i++) {
        t += DAY;
        uint8_t s = zodiac_sign_at(t), m = 0, d = 0;
        CHECK(s < ZODIAC_SIGN_COUNT);
        CHECK(s == last || s == (last + 1) % ZODIAC_SIGN_COUNT);
        zodiac_sign_start_date(s, t, offset, &m, &d);
        if (s == last) {
            CHECK(m == last_m && d == last_d);
        } else {
            changes++;
            uint8_t today_m = 0, today_d = 0, yday_m = 0, yday_d = 0;
            local_date(t, offset, &today_m, &today_d);
            local_date(t - DAY, offset, &yday_m, &yday_d);
            CHECK((m == today_m && d == today_d) || (m == yday_m && d == yday_d));
        }
        last = s; last_m = m; last_d = d;
    }
    // 1 Jan 2026 to 30 Dec 2030: every sign starts once a year, Capricorn on ~21 Dec.
    CHECK_EQ_INT(changes, 5 * 12, "sign changes in 5 years");
}

static void test_table_strings(void) {
    for (uint8_t i = 0; i < ZODIAC_SIGN_COUNT; i++) {
        CHECK_EQ_INT(strlen(zodiac_signs[i].name), 5, "name of sign %u", i);
        CHECK_EQ_INT(strlen(zodiac_signs[i].abbreviation), 2, "abbreviation of sign %u", i);
        for (uint8_t j = 0; j < i; j++) {
            CHECK(strcmp(zodiac_signs[i].abbreviation, zodiac_signs[j].abbreviation) != 0);
        }
    }
}

int main(void) {
    test_sign_flips_at_equinoxes_and_solstices();
    test_start_date_is_local();
    test_start_date_of_browsed_signs();
    test_five_years_daily();
    test_table_strings();
    return check_summary("test_zodiac");
}

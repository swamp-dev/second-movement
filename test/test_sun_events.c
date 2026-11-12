#include <math.h>
#include <stdbool.h>

#include "check.h"
#include "sun_events.h"
#include "sunriset.h"

static int64_t floor_div(int64_t a, int64_t b) {
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's algorithm).
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = floor_div(y, 400);
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// Unix time of a local wall-clock time at the given UTC offset (seconds).
static int64_t local_time(int y, int mo, int d, int h, int mi, int32_t offset) {
    return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 - offset;
}

// sunriset's own answer for a UT date, as Unix times. Only valid away from the
// antimeridian, where sunriset needs no correction.
static void oracle(int y, int m, int d, double lat, double lon, int64_t *rise, int64_t *set) {
    double rise_h, set_h;
    CHECK(sun_rise_set(y, m, d, lon, lat, &rise_h, &set_h) == 0);
    int64_t midnight = days_from_civil(y, m, d) * 86400;
    *rise = midnight + (int64_t)floor(rise_h * 3600.0 + 0.5);
    *set = midnight + (int64_t)floor(set_h * 3600.0 + 0.5);
}

// Hours past local mean midnight, from 0 to 24.
static double solar_hour(int64_t t, double lon) {
    int64_t s = t + (int64_t)(lon * 240.0);
    return (double)(s - floor_div(s, 86400) * 86400) / 3600.0;
}

// The #47 report: 30 N, 40 E, on a watch at UTC-5 reading 21:20 on 11 November
// 2026. The next sunrise is 22:43 on the 11th by the watch (03:43 UTC on the
// 12th), and the sunset after it is on the 12th. The face showed 0:00 on the
// 12th, because the local hour of the sunrise was negative.
static void test_issue_47_east_of_the_watch_zone(void) {
    const int32_t offset = -5 * 3600;
    int64_t now = local_time(2026, 11, 11, 21, 20, offset);
    int64_t rise, set;
    oracle(2026, 11, 12, 30.0, 40.0, &rise, &set);

    sun_event_t e = sun_events_upcoming(now, 0, 30.0, 40.0);
    CHECK_EQ_INT(e.kind, SUN_EVENT_RISE, "next event");
    CHECK_EQ_INT(e.t, rise, "next sunrise");
    CHECK(e.t > local_time(2026, 11, 11, 22, 30, offset));
    CHECK(e.t < local_time(2026, 11, 11, 23, 0, offset));

    e = sun_events_upcoming(now, 1, 30.0, 40.0);
    CHECK_EQ_INT(e.kind, SUN_EVENT_SET, "event after next");
    CHECK_EQ_INT(e.t, set, "sunset after it");
}

// Honolulu on a watch at UTC+9: the sunset of a Honolulu date falls on the next
// date by the watch.
static void test_west_of_the_watch_zone(void) {
    const double lat = 21.31, lon = -157.86;
    const int32_t offset = 9 * 3600;
    int64_t now = local_time(2026, 7, 1, 12, 0, offset);  // 03:00 UTC on 1 July
    int64_t rise, set;
    oracle(2026, 7, 1, lat, lon, &rise, &set);  // 16:00 and 05:16 UTC: sunset is the 2nd's

    sun_event_t e = sun_events_upcoming(now, 0, lat, lon);
    CHECK_EQ_INT(e.kind, SUN_EVENT_SET, "next event");
    int64_t prev_rise, prev_set;
    oracle(2026, 6, 30, lat, lon, &prev_rise, &prev_set);
    CHECK_EQ_INT(e.t, prev_set, "sunset of 30 June, UT");
    e = sun_events_upcoming(now, 1, lat, lon);
    CHECK_EQ_INT(e.kind, SUN_EVENT_RISE, "event after next");
    CHECK_EQ_INT(e.t, rise, "sunrise of 1 July, UT");
}

typedef struct {
    const char *name;
    double lat, lon;
} place_t;

// Latitudes within 45 degrees, where every day has a sunrise and a sunset, on
// both sides of the antimeridian and near the prime meridian.
static const place_t places[] = {
    {"New York", 40.71, -74.01},
    {"Honolulu", 21.31, -157.86},
    {"London-ish", 44.0, -0.13},
    {"Singapore", 1.35, 103.82},
    {"Sydney", -33.87, 151.21},
    {"Suva", -18.14, 178.44},
    {"Tonga", -21.14, -175.20},
    {"Kiritimati", 1.87, -157.43},
    {"Date line east", 0.5, 179.9},
    {"Date line west", -0.5, -179.9},
    {"Issue 47", 30.0, 40.0},
};

// Over a year, from every 7th hour: the next events come in order, alternate
// between sunrise and sunset, sunrises fall in the local morning and sunsets in
// the evening, and none is skipped (the next one is less than a night away).
static void test_sequence_properties(void) {
    int64_t start = local_time(2026, 1, 1, 0, 0, 0);
    for (size_t p = 0; p < sizeof(places) / sizeof(places[0]); p++) {
        const place_t *pl = &places[p];
        int bad = 0;
        for (int64_t now = start; now < start + 366 * 86400 && bad < 3; now += 7 * 3600) {
            sun_event_t e[3];
            for (unsigned i = 0; i < 3; i++) e[i] = sun_events_upcoming(now, i, pl->lat, pl->lon);
            bool ok = e[0].t > now && e[0].t - now < 16 * 3600 && e[1].t > e[0].t && e[2].t > e[1].t;
            for (int i = 0; i < 3; i++) {
                double h = solar_hour(e[i].t, pl->lon);
                if (e[i].kind == SUN_EVENT_RISE) ok = ok && h > 3.0 && h < 9.5;
                else if (e[i].kind == SUN_EVENT_SET) ok = ok && h > 14.5 && h < 21.0;
                else ok = false;
                if (i > 0) ok = ok && e[i].kind != e[i - 1].kind;
            }
            // The event at `t` is not after `t`; one second earlier, it is next.
            ok = ok && sun_events_upcoming(e[0].t - 1, 0, pl->lat, pl->lon).t == e[0].t;
            ok = ok && sun_events_upcoming(e[0].t, 0, pl->lat, pl->lon).t == e[1].t;
            if (!ok) {
                bad++;
                fprintf(stderr, "%s at %lld: %lld/%d %lld/%d %lld/%d\n", pl->name, (long long)now,
                        (long long)e[0].t, e[0].kind, (long long)e[1].t, e[1].kind, (long long)e[2].t, e[2].kind);
            }
        }
        CHECK_EQ_INT(bad, 0, "%s", pl->name);
    }
}

// Tromso has polar night in December and midnight sun in June. A polar day
// takes the place of that day's events, at its local mean noon.
static void test_polar(void) {
    const double lat = 69.65, lon = 18.96;
    int64_t noon_offset = (int64_t)floor((12.0 - lon / 15.0) * 3600.0 + 0.5);

    // 15 December, 12:00 UTC: the 15th's mean noon (10:44 UTC) has passed.
    sun_event_t e = sun_events_upcoming(local_time(2026, 12, 15, 12, 0, 0), 0, lat, lon);
    CHECK_EQ_INT(e.kind, SUN_EVENT_NO_RISE, "December");
    CHECK_EQ_INT(e.t, days_from_civil(2026, 12, 16) * 86400 + noon_offset, "16 December's mean noon");
    // 09:00 UTC, before the 15th's mean noon: the 15th itself.
    e = sun_events_upcoming(local_time(2026, 12, 15, 9, 0, 0), 0, lat, lon);
    CHECK_EQ_INT(e.t, days_from_civil(2026, 12, 15) * 86400 + noon_offset, "15 December's mean noon");

    e = sun_events_upcoming(local_time(2026, 6, 21, 12, 0, 0), 1, lat, lon);
    CHECK_EQ_INT(e.kind, SUN_EVENT_NO_SET, "June");
    CHECK_EQ_INT(e.t, days_from_civil(2026, 6, 23) * 86400 + noon_offset, "23 June's mean noon");

    // Into the polar night: the times only ever increase, and once the polar
    // night has begun, no sunrise or sunset follows until it ends.
    int64_t start = local_time(2026, 11, 1, 0, 0, 0);
    int bad = 0, nights = 0;
    for (int64_t now = start; now < start + 61 * 86400; now += 5 * 3600) {
        sun_event_t a = sun_events_upcoming(now, 0, lat, lon);
        sun_event_t b = sun_events_upcoming(now, 1, lat, lon);
        if (!(a.t > now && b.t > a.t && b.t - a.t <= 86400 + 3600)) bad++;
        if (a.kind == SUN_EVENT_NO_RISE) nights++;
        if (a.kind == SUN_EVENT_NO_SET || b.kind == SUN_EVENT_NO_SET) bad++;
    }
    CHECK_EQ_INT(bad, 0, "Tromso, November and December");
    CHECK(nights > 100);
}

static void test_skip_is_clamped(void) {
    int64_t now = local_time(2026, 3, 20, 0, 0, 0);
    sun_event_t a = sun_events_upcoming(now, SUN_EVENTS_MAX_AHEAD, 51.51, -0.13);
    sun_event_t b = sun_events_upcoming(now, 99, 51.51, -0.13);
    CHECK_EQ_INT(b.t, a.t, "skip 99");
    CHECK_EQ_INT(b.kind, a.kind, "skip 99");
}

static void test_events_of_day(void) {
    int64_t rise = 1, set = 2;
    // London, 20 March 2026: sunrise about 06:03 UTC, sunset about 18:14 UTC.
    CHECK_EQ_INT(sun_events_of_day(days_from_civil(2026, 3, 20), 51.51, -0.13, &rise, &set), 0, "London");
    CHECK(rise > local_time(2026, 3, 20, 6, 0, 0) && rise < local_time(2026, 3, 20, 6, 8, 0));
    CHECK(set > local_time(2026, 3, 20, 18, 10, 0) && set < local_time(2026, 3, 20, 18, 18, 0));

    // Suva, just west of the antimeridian: sunriset puts the transit a day
    // late; the day's sunrise is still in its own local morning.
    CHECK_EQ_INT(sun_events_of_day(days_from_civil(2026, 3, 20), -18.14, 178.44, &rise, &set), 0, "Suva");
    CHECK(solar_hour(rise, 178.44) > 5.0 && solar_hour(rise, 178.44) < 7.0);
    CHECK(rise - days_from_civil(2026, 3, 20) * 86400 + (int64_t)(178.44 * 240.0) > 0);
    CHECK(rise - days_from_civil(2026, 3, 20) * 86400 + (int64_t)(178.44 * 240.0) < 86400);

    rise = 1; set = 2;
    CHECK_EQ_INT(sun_events_of_day(days_from_civil(2026, 12, 15), 69.65, 18.96, &rise, &set), -1, "polar night");
    CHECK_EQ_INT(sun_events_of_day(days_from_civil(2026, 6, 21), 69.65, 18.96, &rise, &set), 1, "polar day");
    CHECK_EQ_INT(rise, 1, "rise unchanged");
    CHECK_EQ_INT(set, 2, "set unchanged");
}

int main(void) {
    test_issue_47_east_of_the_watch_zone();
    test_west_of_the_watch_zone();
    test_sequence_properties();
    test_polar();
    test_skip_is_clamped();
    test_events_of_day();
    return check_summary("test_sun_events");
}

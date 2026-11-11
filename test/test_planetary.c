#include <string.h>

#include "check.h"
#include "planetary.h"

enum { SATURN, JUPITER, MARS, SUN, VENUS, MERCURY, MOON };

// Ruler of the first hour of each weekday, Sunday first.
static const uint8_t day_ruler[7] = {SUN, MOON, MARS, MERCURY, JUPITER, VENUS, SATURN};

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

// 0 = Sunday. 1970-01-01 was a Thursday.
static int local_weekday(int64_t t, int32_t offset) {
    return (int)((floor_div(t + offset, 86400) % 7 + 11) % 7);
}

typedef struct {
    const char *name;
    double lat, lon;
    int32_t offset;
} place_t;

static const place_t london = {"London", 51.51, -0.13, 0};
static const place_t reykjavik = {"Reykjavik", 64.15, -21.94, 0};
static const place_t tonga = {"Tonga", -21.14, -175.20, 13 * 3600};
static const place_t suva = {"Suva", -18.14, 178.44, 12 * 3600};
static const place_t tromso = {"Tromso", 69.65, 18.96, 3600};

// The locations of the #8 report, with their standard-time offsets.
static const place_t places[] = {
    {"New York", 40.71, -74.01, -5 * 3600},
    {"Los Angeles", 34.05, -118.24, -8 * 3600},
    {"Honolulu", 21.31, -157.86, -10 * 3600},
    london,
    {"Tokyo", 35.68, 139.69, 9 * 3600},
    {"Singapore", 1.35, 103.82, 8 * 3600},
    {"Urumqi", 43.83, 87.62, 8 * 3600},
    {"Sydney", -33.87, 151.21, 10 * 3600},
    {"Auckland", -36.85, 174.76, 12 * 3600},
    reykjavik,
    {"New York on UTC", 40.71, -74.01, 0},
    {"Adak", 51.88, -176.66, -10 * 3600},
    {"Anadyr", 64.73, 177.51, 12 * 3600},
    {"Gisborne", -38.66, 178.02, 12 * 3600},
    suva,
    {"Chatham", -43.95, -176.55, 12 * 3600 + 45 * 60},
    {"Attu", 52.93, 172.91, -10 * 3600},
    tonga,
    {"Samoa", -13.83, -171.76, 13 * 3600},
    {"Kiritimati", 1.87, -157.43, 14 * 3600},
};

static bool at(const place_t *p, int64_t t, planetary_hour_t *h) {
    return planetary_hour_at(t, p->offset, p->lat, p->lon, h);
}

// London, Friday 20 March 2026: sunrise is at about 06:03 UTC, so the first
// hour is Venus (Friday), and the next 24 hours run through the Chaldean order
// into Saturday's first hour, Saturn.
static void test_chaldean_sequence_over_a_day(void) {
    planetary_hour_t h;
    CHECK(at(&london, local_time(2026, 3, 20, 6, 30, 0), &h));
    CHECK_EQ_INT(h.hour_number, 0, "London 06:30");
    CHECK_EQ_INT(h.ruler, VENUS, "London 06:30");
    CHECK(h.start >= local_time(2026, 3, 20, 5, 58, 0) && h.start <= local_time(2026, 3, 20, 6, 8, 0));

    for (int k = 1; k <= 24; k++) {
        int64_t prev_end = h.end;
        CHECK(at(&london, prev_end, &h));
        CHECK_EQ_INT(h.start, prev_end, "hour %d starts where the last ended", k);
        CHECK_EQ_INT(h.hour_number, k % 24, "hour %d", k);
        CHECK_EQ_INT(h.ruler, (VENUS + k) % 7, "hour %d", k);
    }
    CHECK_EQ_INT(h.ruler, SATURN, "Saturday's first hour");
}

// 03:00 on Saturday is still Friday's night, so it takes Friday's ruler.
static void test_before_sunrise_uses_previous_day(void) {
    planetary_hour_t h;
    CHECK(at(&london, local_time(2026, 3, 21, 3, 0, 0), &h));
    CHECK(h.hour_number >= 12 && h.hour_number <= 23);
    CHECK_EQ_INT(h.ruler, (VENUS + h.hour_number) % 7, "night hour %u", h.hour_number);
}

// Sunset in Reykjavik on 20 June is just after midnight, so 00:00 on Sunday 21
// June is still the last day hour of Saturday.
static void test_reykjavik_midsummer_midnight(void) {
    planetary_hour_t h;
    CHECK(at(&reykjavik, local_time(2026, 6, 21, 0, 0, 0), &h));
    CHECK_EQ_INT(h.hour_number, 11, "Reykjavik 00:00 21 June");
    CHECK_EQ_INT(h.ruler, (SATURN + 11) % 7, "Reykjavik 00:00 21 June");
}

// Midday on Tuesday 10 November 2026 is a day hour of Tuesday, ruled from Mars,
// on both sides of the date line.
static void test_date_line_midday(void) {
    const place_t *ps[] = {&tonga, &suva};
    for (int i = 0; i < 2; i++) {
        planetary_hour_t h;
        CHECK(at(ps[i], local_time(2026, 11, 10, 12, 0, ps[i]->offset), &h));
        CHECK(h.hour_number <= 11);
        CHECK_EQ_INT(h.ruler, (MARS + h.hour_number) % 7, "%s midday, hour %u", ps[i]->name, h.hour_number);
    }
}

static void test_polar_day_and_night(void) {
    planetary_hour_t h;
    memset(&h, 0xAB, sizeof h);
    planetary_hour_t untouched = h;
    CHECK(!at(&tromso, local_time(2026, 6, 21, 12, 0, tromso.offset), &h));
    CHECK(!at(&tromso, local_time(2026, 12, 21, 12, 0, tromso.offset), &h));
    CHECK(memcmp(&h, &untouched, sizeof h) == 0);
}

// Walks every planetary hour of 2026 and counts the hours that break a rule:
// the hour contains its own start and midpoint, the next hour starts where it
// ends with the next hour number and the next ruler, each hour 0 has the ruler
// of the local weekday it starts on, and hours last between 1 minute and 3 hours.
// Returns the number of instants with no planetary hour.
static int walk_year(const place_t *p, int *bad) {
    const int64_t year_end = local_time(2027, 1, 1, 0, 0, p->offset);
    int64_t t = local_time(2026, 1, 1, 0, 0, p->offset);
    int missing = 0;
    bool have_prev = false;
    planetary_hour_t prev = {0}, h;

    *bad = 0;
    while (t < year_end) {
        if (!at(p, t, &h)) {
            missing++;
            have_prev = false;
            t += 1800;
            continue;
        }
        int64_t mid = h.start + (h.end - h.start) / 2;
        planetary_hour_t m;
        bool ok = h.start <= t && t < h.end
            && h.end - h.start >= 60 && h.end - h.start <= 3 * 3600
            && at(p, mid, &m) && m.start == h.start && m.hour_number == h.hour_number
            && h.ruler < PLANETARY_PLANET_COUNT && h.hour_number < 24;
        if (have_prev) {
            ok = ok && h.start == prev.end
                && h.hour_number == (prev.hour_number + 1) % 24
                && h.ruler == (prev.ruler + 1) % 7;
        }
        if (h.hour_number == 0) ok = ok && h.ruler == day_ruler[local_weekday(h.start, p->offset)];
        if (!ok && (*bad)++ < 3) {
            fprintf(stderr, "  %s: bad hour %u (ruler %u) %lld..%lld for t=%lld\n", p->name,
                    h.hour_number, h.ruler, (long long)h.start, (long long)h.end, (long long)t);
        }
        prev = h;
        have_prev = true;
        t = h.end;
    }
    return missing;
}

static void test_whole_year_everywhere(void) {
    for (size_t i = 0; i < sizeof places / sizeof places[0]; i++) {
        int bad;
        int missing = walk_year(&places[i], &bad);
        CHECK_EQ_INT(missing, 0, "%s: instants with no planetary hour", places[i].name);
        CHECK_EQ_INT(bad, 0, "%s: hours breaking the rules", places[i].name);
    }
}

// Next to polar day and night some days have no sunrise; every hour that is
// returned still follows the rules.
static void test_whole_year_tromso(void) {
    int bad;
    int missing = walk_year(&tromso, &bad);
    CHECK(missing > 0);
    CHECK_EQ_INT(bad, 0, "Tromso: hours breaking the rules");
}

static bool step(const place_t *p, const planetary_hour_t *from, int steps, planetary_hour_t *h) {
    return planetary_hour_step(from, steps, p->offset, p->lat, p->lon, h);
}

// One press at a time, `presses` times in direction `dir` (+1 or -1), from the
// hour containing `t`: each press must land on the neighbouring hour, so no hour
// is skipped or shown twice, whatever the hours' length (#5).
static void check_presses(const place_t *p, int64_t t, int dir, int presses) {
    planetary_hour_t h, next;
    CHECK(at(p, t, &h));
    for (int k = 1; k <= presses; k++) {
        if (!step(p, &h, dir, &next)) {
            CHECK_EQ_INT(k, 0, "%s: press %d failed", p->name, k);
            return;
        }
        bool ok = dir > 0 ? next.start == h.end : next.end == h.start;
        ok = ok && next.hour_number == (h.hour_number + 24 + dir) % 24
            && next.ruler == (h.ruler + 7 + dir) % 7;
        CHECK_EQ_INT(ok, 1, "%s: press %d from hour %u (%lld..%lld) gave hour %u (%lld..%lld)",
                     p->name, k, h.hour_number, (long long)h.start, (long long)h.end,
                     next.hour_number, (long long)next.start, (long long)next.end);
        h = next;
    }
}

// The #5 report: New York, Tuesday 15 December 2026 from 09:40, where day hours
// last about 46 minutes and night hours about 74. Clock-hour steps skipped the
// report's hours 7 and 12 and repeated hour 13 (it counts from 1; hour_number
// counts from 0).
static void test_step_forward_short_and_long_hours(void) {
    const place_t new_york = places[0];
    planetary_hour_t h, s;
    CHECK(at(&new_york, local_time(2026, 12, 15, 9, 40, new_york.offset), &h));
    CHECK_EQ_INT(h.hour_number, 3, "New York 09:40");
    CHECK_EQ_INT(h.ruler, MERCURY, "New York 09:40");
    CHECK(step(&new_york, &h, 3, &s));
    CHECK_EQ_INT(s.hour_number, 6, "three presses from hour 3");
    CHECK_EQ_INT(s.ruler, JUPITER, "three presses from hour 3, the one clock hours skipped");

    // Two days forward from there, across sunsets, midnights and sunrises.
    check_presses(&new_york, local_time(2026, 12, 15, 9, 40, new_york.offset), +1, 48);
}

// The #5 report going backward: London, 20 June 2026 from 14:00, with 75-minute
// day hours, back across sunrise, midnight and the previous sunset.
static void test_step_backward_across_sunrise_sunset_midnight(void) {
    check_presses(&london, local_time(2026, 6, 20, 14, 0, 0), -1, 48);
    check_presses(&reykjavik, local_time(2026, 6, 21, 3, 0, 0), -1, 48);
}

// Every place, both directions, a week of presses from the March equinox
// and from both solstices.
static void test_step_everywhere(void) {
    const int dates[][2] = {{3, 20}, {6, 21}, {12, 21}};
    for (size_t i = 0; i < sizeof places / sizeof places[0]; i++) {
        for (size_t d = 0; d < 3; d++) {
            int64_t t = local_time(2026, dates[d][0], dates[d][1], 12, 0, places[i].offset);
            check_presses(&places[i], t, +1, 24 * 7);
            check_presses(&places[i], t, -1, 24 * 7);
        }
    }
}

// Stepping n at once is the same as n single presses, and 0 steps is the hour itself.
static void test_step_many_equals_repeated_single(void) {
    planetary_hour_t h, many, single;
    CHECK(at(&london, local_time(2026, 3, 20, 18, 0, 0), &h));
    CHECK(step(&london, &h, 0, &many));
    CHECK(memcmp(&many, &h, sizeof h) == 0);

    const int counts[] = {30, -30};
    for (int c = 0; c < 2; c++) {
        CHECK(step(&london, &h, counts[c], &many));
        single = h;
        for (int k = 0; k < 30; k++) CHECK(step(&london, &single, counts[c] > 0 ? 1 : -1, &single));
        CHECK(memcmp(&many, &single, sizeof h) == 0);
    }
}

// Stepping into polar day fails and leaves *out alone: Tromso's last sunset
// before the midnight sun is on about 17 May 2026.
static void test_step_into_polar_day(void) {
    planetary_hour_t h, out;
    CHECK(at(&tromso, local_time(2026, 5, 15, 12, 0, tromso.offset), &h));
    memset(&out, 0xAB, sizeof out);
    planetary_hour_t untouched = out;
    CHECK(!step(&tromso, &h, 24 * 10, &out));
    CHECK(memcmp(&out, &untouched, sizeof out) == 0);
}

static void test_table_strings(void) {
    for (int i = 0; i < PLANETARY_PLANET_COUNT; i++) {
        CHECK_EQ_INT(strlen(planetary_planets[i].name), 5, "name of planet %d", i);
        CHECK_EQ_INT(strlen(planetary_planets[i].abbreviation), 2, "abbreviation of planet %d", i);
    }
    CHECK(strcmp(planetary_planets[SATURN].abbreviation, "SA") == 0);
    CHECK(strcmp(planetary_planets[MOON].abbreviation, "MO") == 0);
}

int main(void) {
    test_chaldean_sequence_over_a_day();
    test_before_sunrise_uses_previous_day();
    test_reykjavik_midsummer_midnight();
    test_date_line_midday();
    test_polar_day_and_night();
    test_whole_year_everywhere();
    test_whole_year_tromso();
    test_step_forward_short_and_long_hours();
    test_step_backward_across_sunrise_sunset_midnight();
    test_step_everywhere();
    test_step_many_equals_repeated_single();
    test_step_into_polar_day();
    test_table_strings();
    return check_summary("test_planetary");
}

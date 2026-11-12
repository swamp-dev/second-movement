// Host tests for lib/zone_offset, against the real utz zone table. The
// expected changes are the IANA tz database's for 2026.

#include "check.h"
#include "zone_offset.h"
#include "zones.h"

static udatetime_t at(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
    udatetime_t dt = {0};
    dt.date.year = UYEAR_FROM_YEAR(year);
    dt.date.month = month;
    dt.date.dayofmonth = day;
    dt.date.dayofweek = dayofweek(dt.date.year, month, day);
    dt.time.hour = hour;
    dt.time.minute = minute;
    return dt;
}

// One DST change: the offset is `before` up to the minute before the UTC
// time given, and `after` from that time on.
typedef struct {
    const char *name;
    uint8_t zone;
    uint8_t month, day, hour, minute;
    int16_t before, after;
} change_t;

static const change_t changes_2026[] = {
    // Rules written in UTC (the EU's 1:00u, Chile's 3:00u and 4:00u).
    {"Berlin spring",    UTZ_BERLIN,     3, 29,  1, 0,   60,  120},
    {"Berlin autumn",    UTZ_BERLIN,    10, 25,  1, 0,  120,   60},
    {"Helsinki spring",  UTZ_HELSINKI,   3, 29,  1, 0,  120,  180},
    {"Helsinki autumn",  UTZ_HELSINKI,  10, 25,  1, 0,  180,  120},
    {"London spring",    UTZ_LONDON,     3, 29,  1, 0,    0,   60},
    {"London autumn",    UTZ_LONDON,    10, 25,  1, 0,   60,    0},
    {"Nuuk spring",      UTZ_NUUK,       3, 29,  1, 0, -120,  -60},
    {"Nuuk autumn",      UTZ_NUUK,      10, 25,  1, 0,  -60, -120},
    {"Santiago autumn",  UTZ_SANTIAGO,   4,  5,  3, 0, -180, -240},
    {"Santiago spring",  UTZ_SANTIAGO,   9,  6,  4, 0, -240, -180},
    // Rules written in local time; the local day differs from the UTC day
    // for Auckland, Sydney and Cairo.
    {"New York spring",  UTZ_NEW_YORK,   3,  8,  7, 0, -300, -240},
    {"New York autumn",  UTZ_NEW_YORK,  11,  1,  6, 0, -240, -300},
    {"Los Angeles spring", UTZ_LOS_ANGELES, 3, 8, 10, 0, -480, -420},
    {"Los Angeles autumn", UTZ_LOS_ANGELES, 11, 1, 9, 0, -420, -480},
    {"Cairo spring",     UTZ_CAIRO,      4, 23, 22, 0,  120,  180},
    {"Cairo autumn",     UTZ_CAIRO,     10, 29, 21, 0,  180,  120},
    {"Jerusalem spring", UTZ_JERUSALEM,  3, 27,  0, 0,  120,  180},
    {"Jerusalem autumn", UTZ_JERUSALEM, 10, 24, 23, 0,  180,  120},
    {"Sydney autumn",    UTZ_SYDNEY,     4,  4, 16, 0,  660,  600},
    {"Sydney spring",    UTZ_SYDNEY,    10,  3, 16, 0,  600,  660},
    {"Auckland autumn",  UTZ_AUCKLAND,   4,  4, 14, 0,  780,  720},
    {"Auckland spring",  UTZ_AUCKLAND,   9, 26, 14, 0,  720,  780},
};

static void test_changes_happen_at_the_iana_instant(void) {
    for (size_t i = 0; i < sizeof(changes_2026) / sizeof(changes_2026[0]); i++) {
        const change_t *c = &changes_2026[i];
        const uzone_packed_t *zone = &zone_defns[c->zone];
        // Every table row is on the hour, so a minute earlier is the hour before.
        udatetime_t just_before = at(2026, c->month, c->day, c->hour ? c->hour - 1 : 23, 59);
        if (!c->hour) just_before = at(2026, c->month, c->day - 1, 23, 59);

        CHECK_EQ_INT(zone_offset_at_utc(zone, just_before), c->before, "%s, a minute before", c->name);
        CHECK_EQ_INT(zone_offset_at_utc(zone, at(2026, c->month, c->day, c->hour, c->minute)), c->after, "%s, at the change", c->name);
    }
}

static void test_zone_without_rules(void) {
    CHECK_EQ_INT(zone_offset_at_utc(&zone_defns[UTZ_KOLKATA], at(2026, 7, 1, 12, 0)), 330, "Kolkata");
    CHECK_EQ_INT(zone_offset_at_utc(&zone_defns[UTZ_UTC], at(2026, 7, 1, 12, 0)), 0, "UTC");
}

static void test_local_day_in_another_year(void) {
    // 13:00 UTC on 31 December is already 1 January in Auckland, in summer time.
    CHECK_EQ_INT(zone_offset_at_utc(&zone_defns[UTZ_AUCKLAND], at(2025, 12, 31, 13, 0)), 780, "Auckland, new year");
    // 03:00 UTC on 1 January is still 31 December in New York.
    CHECK_EQ_INT(zone_offset_at_utc(&zone_defns[UTZ_NEW_YORK], at(2026, 1, 1, 3, 0)), -300, "New York, new year");
    // 23:30 UTC on 28 February 2028 (a leap year) is 29 February in Berlin.
    CHECK_EQ_INT(zone_offset_at_utc(&zone_defns[UTZ_BERLIN], at(2028, 2, 28, 23, 30)), 60, "Berlin, leap day");
}

static void test_standard_time_input(void) {
    // Berlin standard time is UTC+1, so the 01:00 UTC changes are at 02:00.
    const uzone_packed_t *berlin = &zone_defns[UTZ_BERLIN];
    CHECK_EQ_INT(zone_offset_at_standard(berlin, at(2026, 3, 29, 1, 59)), 60, "Berlin spring, 01:59 standard");
    CHECK_EQ_INT(zone_offset_at_standard(berlin, at(2026, 3, 29, 2, 0)), 120, "Berlin spring, 02:00 standard");
    CHECK_EQ_INT(zone_offset_at_standard(berlin, at(2026, 10, 25, 1, 59)), 120, "Berlin autumn, 01:59 standard");
    CHECK_EQ_INT(zone_offset_at_standard(berlin, at(2026, 10, 25, 2, 0)), 60, "Berlin autumn, 02:00 standard");

    // Santiago standard time is UTC-4: 03:00 UTC on 5 April is 23:00 on the 4th.
    const uzone_packed_t *santiago = &zone_defns[UTZ_SANTIAGO];
    CHECK_EQ_INT(zone_offset_at_standard(santiago, at(2026, 4, 4, 22, 59)), -180, "Santiago autumn, 22:59 standard");
    CHECK_EQ_INT(zone_offset_at_standard(santiago, at(2026, 4, 4, 23, 0)), -240, "Santiago autumn, 23:00 standard");

    // New York's rules are already in standard time: 2:00 EST, and 2:00 EDT = 1:00 EST.
    const uzone_packed_t *new_york = &zone_defns[UTZ_NEW_YORK];
    CHECK_EQ_INT(zone_offset_at_standard(new_york, at(2026, 3, 8, 1, 59)), -300, "New York spring, 01:59 standard");
    CHECK_EQ_INT(zone_offset_at_standard(new_york, at(2026, 3, 8, 2, 0)), -240, "New York spring, 02:00 standard");
    CHECK_EQ_INT(zone_offset_at_standard(new_york, at(2026, 11, 1, 0, 59)), -240, "New York autumn, 00:59 standard");
    CHECK_EQ_INT(zone_offset_at_standard(new_york, at(2026, 11, 1, 1, 0)), -300, "New York autumn, 01:00 standard");
}

static void test_wall_clock_input(void) {
    // The offset depends only on the date and time given, not on any "current" one:
    // noon on 5 April is in summer time whatever the date was before.
    const uzone_packed_t *new_york = &zone_defns[UTZ_NEW_YORK];
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 3, 5, 12, 0)), -300, "New York, 5 March noon");
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 4, 5, 12, 0)), -240, "New York, 5 April noon");

    // Clocks go forward at 02:00 on 8 March: 01:59 EST is followed by 03:00 EDT.
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 3, 8, 1, 59)), -300, "New York spring, 01:59");
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 3, 8, 3, 0)), -240, "New York spring, 03:00");
    // 02:30 doesn't exist. The winter offset puts it at 07:30 UTC, which is 03:30 EDT.
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 3, 8, 2, 30)), -300, "New York spring, 02:30 (skipped)");

    // Clocks go back at 02:00 EDT on 1 November: 01:00 to 01:59 happens twice.
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 11, 1, 0, 59)), -240, "New York autumn, 00:59");
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 11, 1, 1, 30)), -300, "New York autumn, 01:30 (the second one)");
    CHECK_EQ_INT(zone_offset_at_local(new_york, at(2026, 11, 1, 2, 0)), -300, "New York autumn, 02:00");

    // Berlin's rules are in UTC: 01:00 UTC is 02:00 CET and 03:00 CEST.
    const uzone_packed_t *berlin = &zone_defns[UTZ_BERLIN];
    CHECK_EQ_INT(zone_offset_at_local(berlin, at(2026, 3, 29, 1, 59)), 60, "Berlin spring, 01:59");
    CHECK_EQ_INT(zone_offset_at_local(berlin, at(2026, 3, 29, 2, 30)), 60, "Berlin spring, 02:30 (skipped)");
    CHECK_EQ_INT(zone_offset_at_local(berlin, at(2026, 3, 29, 3, 0)), 120, "Berlin spring, 03:00");
    CHECK_EQ_INT(zone_offset_at_local(berlin, at(2026, 10, 25, 1, 59)), 120, "Berlin autumn, 01:59");
    CHECK_EQ_INT(zone_offset_at_local(berlin, at(2026, 10, 25, 2, 30)), 60, "Berlin autumn, 02:30 (the second one)");
    CHECK_EQ_INT(zone_offset_at_local(berlin, at(2026, 10, 25, 3, 0)), 60, "Berlin autumn, 03:00");

    // Sydney: summer time ends at 03:00 on 5 April and starts at 02:00 on 4 October,
    // both on the previous day in UTC.
    const uzone_packed_t *sydney = &zone_defns[UTZ_SYDNEY];
    CHECK_EQ_INT(zone_offset_at_local(sydney, at(2026, 4, 5, 1, 59)), 660, "Sydney autumn, 01:59");
    CHECK_EQ_INT(zone_offset_at_local(sydney, at(2026, 4, 5, 3, 0)), 600, "Sydney autumn, 03:00");
    CHECK_EQ_INT(zone_offset_at_local(sydney, at(2026, 10, 4, 1, 59)), 600, "Sydney spring, 01:59");
    CHECK_EQ_INT(zone_offset_at_local(sydney, at(2026, 10, 4, 2, 30)), 600, "Sydney spring, 02:30 (skipped)");
    CHECK_EQ_INT(zone_offset_at_local(sydney, at(2026, 10, 4, 3, 0)), 660, "Sydney spring, 03:00");

    // 00:30 on 1 January in Auckland is still the old year in UTC.
    CHECK_EQ_INT(zone_offset_at_local(&zone_defns[UTZ_AUCKLAND], at(2026, 1, 1, 0, 30)), 780, "Auckland, new year");
    CHECK_EQ_INT(zone_offset_at_local(&zone_defns[UTZ_KOLKATA], at(2026, 7, 1, 12, 0)), 330, "Kolkata");
}

static void test_each_zone_uses_one_clock_for_its_rules(void) {
    // zone_offset reads the clock from a zone's first rule.
    for (size_t z = 0; z < NUM_ZONE_NAMES; z++) {
        const urule_packed_t *rules = &zone_rules[zone_defns[z].rules_idx];
        for (uint8_t r = 1; r < zone_defns[z].rules_len; r++) {
            CHECK_EQ_INT(rules[r].at_is_local_time, rules[0].at_is_local_time, "zone %zu, rule %u", z, r);
        }
    }
}

int main(void) {
    test_each_zone_uses_one_clock_for_its_rules();
    test_changes_happen_at_the_iana_instant();
    test_zone_without_rules();
    test_local_day_in_another_year();
    test_standard_time_input();
    test_wall_clock_input();
    return check_summary("test_zone_offset");
}

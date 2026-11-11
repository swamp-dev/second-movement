#include <string.h>

#include "check.h"
#include "zodiac.h"

static const uint8_t days_in_month[13] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// The day before month/day, with 1 January wrapping to 31 December.
static void previous_day(uint8_t *month, uint8_t *day) {
    if (*day > 1) {
        (*day)--;
    } else {
        *month = (*month == 1) ? 12 : *month - 1;
        *day = days_in_month[*month];
    }
}

static void test_every_cusp(void) {
    for (uint8_t i = 0; i < ZODIAC_SIGN_COUNT; i++) {
        uint8_t m = zodiac_signs[i].start_month, d = zodiac_signs[i].start_day;
        uint8_t prev = (i == 0) ? ZODIAC_SIGN_COUNT - 1 : i - 1;

        CHECK_EQ_INT(zodiac_sign_for_date(m, d), i, "start day %u/%u of %s", m, d, zodiac_signs[i].name);
        previous_day(&m, &d);
        CHECK_EQ_INT(zodiac_sign_for_date(m, d), prev, "day %u/%u, before %s starts", m, d, zodiac_signs[i].name);
    }
}

static void test_capricorn_across_new_year(void) {
    const uint8_t capricorn = ZODIAC_SIGN_COUNT - 1;
    CHECK_EQ_INT(zodiac_sign_for_date(12, 31), capricorn, "31 Dec");
    CHECK_EQ_INT(zodiac_sign_for_date(1, 1), capricorn, "1 Jan");
    CHECK_EQ_INT(zodiac_sign_for_date(1, 19), capricorn, "19 Jan");
    CHECK_EQ_INT(zodiac_sign_for_date(1, 20), 0, "20 Jan is Aquarius");
}

// Walking the calendar from 1 January, the sign either stays the same or moves
// to the next one, and every sign appears.
static void test_whole_year_is_monotonic(void) {
    uint8_t seen[ZODIAC_SIGN_COUNT] = {0};
    uint8_t last = zodiac_sign_for_date(1, 1);
    for (uint8_t m = 1; m <= 12; m++) {
        for (uint8_t d = 1; d <= days_in_month[m]; d++) {
            uint8_t s = zodiac_sign_for_date(m, d);
            CHECK(s < ZODIAC_SIGN_COUNT);
            CHECK(s == last || s == (last + 1) % ZODIAC_SIGN_COUNT);
            if (s < ZODIAC_SIGN_COUNT) seen[s] = 1;
            last = s;
        }
    }
    for (uint8_t i = 0; i < ZODIAC_SIGN_COUNT; i++) CHECK(seen[i]);
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
    test_every_cusp();
    test_capricorn_across_new_year();
    test_whole_year_is_monotonic();
    test_table_strings();
    return check_summary("test_zodiac");
}

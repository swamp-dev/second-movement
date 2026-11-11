/*
 * MIT License
 *
 * Copyright (c) 2026 second-movement contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <math.h>

#include "planetary.h"
#include "sunriset.h"

const planetary_planet_t planetary_planets[PLANETARY_PLANET_COUNT] = {
    {"Satur", "SA"},
    {"Jupit", "JU"},
    {"Mars ", "MA"},
    {"Sun  ", "SU"},
    {"Venus", "VE"},
    {"Mercu", "ME"},
    {"Moon ", "MO"},
};

// Index into planetary_planets[] of the ruler of each weekday, Sunday first.
static const uint8_t _day_ruler[7] = {3, 6, 2, 5, 1, 4, 0};

// Upper limb 35 arc minutes below the horizon, as sun_rise_set() uses.
#define PLANETARY_SUN_ALTITUDE (-35.0 / 60.0)

static int64_t _floor_div(int64_t a, int64_t b) {
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

// Proleptic Gregorian date of a day count since 1970-01-01 (Howard Hinnant's
// algorithm).
static void _civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = _floor_div(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

typedef struct {
    int64_t t;
    bool is_rise;
} sun_event_t;

// Sunrise and sunset of the day whose local mean noon falls on `day` (days since
// 1970-01-01), as Unix times. sunriset wraps the Sun's transit into one UT day,
// which near ±180° longitude lands a day off; shifting the transit back to
// within 12 hours of local mean noon undoes that.
static bool _sun_events(int64_t day, double lat, double lon, sun_event_t *rise, sun_event_t *set) {
    int y, m, d;
    double rise_h, set_h;
    _civil_from_days(day, &y, &m, &d);
    if (__sunriset__(y, m, d, lon, lat, PLANETARY_SUN_ALTITUDE, 1, &rise_h, &set_h) != 0) return false;

    double noon_h = 12.0 - lon / 15.0;
    double transit_h = (rise_h + set_h) / 2.0;
    double shift_h = 0.0;
    if (transit_h - noon_h > 12.0) shift_h = -24.0;
    else if (transit_h - noon_h < -12.0) shift_h = 24.0;

    int64_t midnight = day * 86400;
    rise->t = midnight + (int64_t)floor((rise_h + shift_h) * 3600.0 + 0.5);
    rise->is_rise = true;
    set->t = midnight + (int64_t)floor((set_h + shift_h) * 3600.0 + 0.5);
    set->is_rise = false;
    return true;
}

bool planetary_hour_at(int64_t now, int32_t utc_offset, double latitude, double longitude,
                       planetary_hour_t *out) {
    // Local mean solar day of `now`: the sunrise and sunset of the days either
    // side of it always include the ones that bracket `now`.
    int64_t mean_day = _floor_div(now + (int64_t)(longitude * 240.0), 86400);

    sun_event_t events[6];
    int count = 0;
    for (int64_t day = mean_day - 1; day <= mean_day + 1; day++) {
        if (_sun_events(day, latitude, longitude, &events[count], &events[count + 1])) count += 2;
    }

    for (int i = 1; i < count; i++) {
        sun_event_t e = events[i];
        int j = i;
        for (; j > 0 && events[j - 1].t > e.t; j--) events[j] = events[j - 1];
        events[j] = e;
    }

    // The last event at or before `now`; the next one must follow it.
    int k = -1;
    for (int i = 0; i < count && events[i].t <= now; i++) k = i;
    if (k < 0 || k + 1 >= count) return false;

    const sun_event_t *sunrise;
    uint8_t first_hour;
    if (events[k].is_rise && !events[k + 1].is_rise) {
        sunrise = &events[k];
        first_hour = 0;
    } else if (!events[k].is_rise && events[k + 1].is_rise && k >= 1 && events[k - 1].is_rise) {
        sunrise = &events[k - 1];  // night hours belong to the preceding sunrise's day
        first_hour = 12;
    } else {
        return false;  // two sunrises or two sunsets in a row: polar day or night
    }

    int64_t period_start = events[k].t;
    int64_t length = events[k + 1].t - period_start;
    int64_t index = 12 * (now - period_start) / length;
    if (index < 11 && period_start + length * (index + 1) / 12 <= now) index++;

    int weekday = (int)((_floor_div(sunrise->t + utc_offset, 86400) % 7 + 11) % 7);  // 1970-01-01 was a Thursday
    uint8_t hour_number = (uint8_t)(first_hour + index);

    out->hour_number = hour_number;
    out->ruler = (uint8_t)((_day_ruler[weekday] + hour_number) % PLANETARY_PLANET_COUNT);
    out->start = period_start + length * index / 12;
    out->end = period_start + length * (index + 1) / 12;
    return true;
}

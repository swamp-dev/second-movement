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

#include "planetary.h"
#include "sun_events.h"

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

static int64_t _floor_div(int64_t a, int64_t b) {
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

typedef struct {
    int64_t t;
    bool is_rise;
} _event_t;

// Sunrise and sunset of the day whose local mean noon falls on `day` (days since
// 1970-01-01). False on a polar day or night.
static bool _sun_events(int64_t day, double lat, double lon, _event_t *rise, _event_t *set) {
    if (sun_events_of_day(day, lat, lon, &rise->t, &set->t) != 0) return false;
    rise->is_rise = true;
    set->is_rise = false;
    return true;
}

bool planetary_hour_at(int64_t now, int32_t utc_offset, double latitude, double longitude,
                       planetary_hour_t *out) {
    // Local mean solar day of `now`: the sunrise and sunset of the days either
    // side of it always include the ones that bracket `now`.
    int64_t mean_day = _floor_div(now + (int64_t)(longitude * 240.0), 86400);

    _event_t events[6];
    int count = 0;
    for (int64_t day = mean_day - 1; day <= mean_day + 1; day++) {
        if (_sun_events(day, latitude, longitude, &events[count], &events[count + 1])) count += 2;
    }

    for (int i = 1; i < count; i++) {
        _event_t e = events[i];
        int j = i;
        for (; j > 0 && events[j - 1].t > e.t; j--) events[j] = events[j - 1];
        events[j] = e;
    }

    // The last event at or before `now`; the next one must follow it.
    int k = -1;
    for (int i = 0; i < count && events[i].t <= now; i++) k = i;
    if (k < 0 || k + 1 >= count) return false;

    const _event_t *sunrise;
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

bool planetary_hour_step(const planetary_hour_t *from, int steps, int32_t utc_offset,
                         double latitude, double longitude, planetary_hour_t *out) {
    // Hours are contiguous, so the next one is the hour containing this one's
    // end, and the previous one the hour containing the second before its start.
    planetary_hour_t h = *from;
    for (; steps > 0; steps--) {
        if (!planetary_hour_at(h.end, utc_offset, latitude, longitude, &h)) return false;
    }
    for (; steps < 0; steps++) {
        if (!planetary_hour_at(h.start - 1, utc_offset, latitude, longitude, &h)) return false;
    }
    *out = h;
    return true;
}

bool planetary_hour_from_now(int64_t now, int steps, int32_t utc_offset, double latitude,
                             double longitude, planetary_hour_t *out, int64_t *changes_at) {
    planetary_hour_t current, h;
    if (!planetary_hour_at(now, utc_offset, latitude, longitude, &current)) return false;
    *changes_at = current.end;
    if (!planetary_hour_step(&current, steps, utc_offset, latitude, longitude, &h)) return false;
    *out = h;
    return true;
}

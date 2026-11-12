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

#include "sun_events.h"
#include "sunriset.h"

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

static int64_t _hours_to_unix(int64_t day, double hours) {
    return day * 86400 + (int64_t)floor(hours * 3600.0 + 0.5);
}

// sunriset wraps the Sun's transit into one UT day, which near +-180 degrees
// longitude lands a day off; shifting the transit back to within 12 hours of
// local mean noon undoes that.
int sun_events_of_day(int64_t day, double latitude, double longitude, int64_t *rise, int64_t *set) {
    int y, m, d;
    double rise_h, set_h;
    _civil_from_days(day, &y, &m, &d);
    int result = sun_rise_set(y, m, d, longitude, latitude, &rise_h, &set_h);
    if (result != 0) return result;

    double noon_h = 12.0 - longitude / 15.0;
    double transit_h = (rise_h + set_h) / 2.0;
    double shift_h = 0.0;
    if (transit_h - noon_h > 12.0) shift_h = -24.0;
    else if (transit_h - noon_h < -12.0) shift_h = 24.0;

    *rise = _hours_to_unix(day, rise_h + shift_h);
    *set = _hours_to_unix(day, set_h + shift_h);
    return 0;
}

sun_event_t sun_events_upcoming(int64_t now, unsigned skip, double latitude, double longitude) {
    if (skip > SUN_EVENTS_MAX_AHEAD) skip = SUN_EVENTS_MAX_AHEAD;

    // Two events a day; the day before the local mean day of `now` can still
    // hold a sunset after it at high latitudes, and each skipped event needs at
    // most one more day.
    enum { DAYS = SUN_EVENTS_MAX_AHEAD + 3 };
    sun_event_t events[2 * DAYS];
    int count = 0;
    int64_t mean_day = _floor_div(now + (int64_t)(longitude * 240.0), 86400);
    for (int64_t day = mean_day - 1; day < mean_day - 1 + DAYS; day++) {
        int64_t rise, set;
        int result = sun_events_of_day(day, latitude, longitude, &rise, &set);
        if (result == 0) {
            events[count++] = (sun_event_t){rise, SUN_EVENT_RISE};
            events[count++] = (sun_event_t){set, SUN_EVENT_SET};
        } else {
            events[count++] = (sun_event_t){_hours_to_unix(day, 12.0 - longitude / 15.0),
                                            result > 0 ? SUN_EVENT_NO_SET : SUN_EVENT_NO_RISE};
        }
    }

    // sunriset computes each day on its own, so near the polar circles a day's
    // sunset can fall after the next day's sunrise; keep time order.
    for (int i = 1; i < count; i++) {
        sun_event_t e = events[i];
        int j = i;
        for (; j > 0 && events[j - 1].t > e.t; j--) events[j] = events[j - 1];
        events[j] = e;
    }

    int i = 0;
    while (i < count - 1 && events[i].t <= now) i++;
    i += (int)skip;
    return events[i < count ? i : count - 1];
}

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

#include "zone_offset.h"

static uint8_t _days_in_month(uint8_t year, uint8_t month) {
    static const uint8_t days[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month] + (month == 2 && is_leap_year(year));
}

// Moves `dt` by `minutes`, which must be less than a day either way.
static void _shift_minutes(udatetime_t *dt, int16_t minutes) {
    int16_t minute_of_day = dt->time.hour * 60 + dt->time.minute + minutes;

    if (minute_of_day < 0) {
        minute_of_day += 24 * 60;
        if (dt->date.dayofmonth > 1) {
            dt->date.dayofmonth--;
        } else {
            if (dt->date.month > 1) {
                dt->date.month--;
            } else {
                dt->date.month = 12;
                dt->date.year--;
            }
            dt->date.dayofmonth = _days_in_month(dt->date.year, dt->date.month);
        }
    } else if (minute_of_day >= 24 * 60) {
        minute_of_day -= 24 * 60;
        if (dt->date.dayofmonth < _days_in_month(dt->date.year, dt->date.month)) {
            dt->date.dayofmonth++;
        } else {
            dt->date.dayofmonth = 1;
            if (dt->date.month < 12) {
                dt->date.month++;
            } else {
                dt->date.month = 1;
                dt->date.year++;
            }
        }
    }

    dt->time.hour = minute_of_day / 60;
    dt->time.minute = minute_of_day % 60;
    dt->date.dayofweek = dayofweek(dt->date.year, dt->date.month, dt->date.dayofmonth);
}

static int16_t _standard_offset(const uzone_packed_t *zone) {
    return (int16_t)zone->offset_inc_minutes * OFFSET_INCREMENT;
}

// `rule_time` is on the clock utz compares with the zone's rules.
static int16_t _offset_at_rule_time(const uzone_packed_t *zone, udatetime_t rule_time) {
    uzone_t unpacked;
    uoffset_t offset;

    unpack_zone(zone, "", &unpacked);
    get_current_offset(&unpacked, &rule_time, &offset);
    return offset.hours * 60 + offset.minutes;
}

int16_t zone_offset_at_utc(const uzone_packed_t *zone, udatetime_t utc) {
    _shift_minutes(&utc, _standard_offset(zone));
    return _offset_at_rule_time(zone, utc);
}

int16_t zone_offset_at_standard(const uzone_packed_t *zone, udatetime_t standard) {
    return _offset_at_rule_time(zone, standard);
}

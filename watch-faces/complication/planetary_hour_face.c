/*
 * MIT License
 *
 * Copyright (c) 2025 Your Name
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
 *
 * Planetary Hour Face
 * This face calculates and displays the current planetary hour based on the user's location and time.
 * Location can be set with an alarm long press, and the planetary hour is determined by the sunrise and sunset times.
 * Once location is set, short press on the alarm buttom will increment the target hour
 *
 */

#include "planetary_hour_face.h"
#include "sunriset.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_rtc.h" // For WATCH_RTC_REFERENCE_YEAR and related RTC definitions
#include <string.h>
#include <math.h> // Ensure math functions are available
#include <stdlib.h>
#include "filesystem.h"
#include <stdio.h> // For debug logging

#define SUNRISE_SUNSET_ALTITUDE (-35.0 / 60.0) // Altitude for sunrise/sunset calculations

#define PLANETARY_HOUR_ERROR 255 // Define an error code for planetary hour calculation failure
#define ZODIAC_SIGN_ERROR 255    // Define an error code for zodiac sign calculation failure

static const uint8_t _location_count = sizeof(longLatPresets) / sizeof(long_lat_presets_t);

// Map of planetary rulers to day of the week values
static const uint8_t week_days_to_chaldean_order[] = {
    3, // Sunday
    6, // Monday
    2, // Tuesday
    5, // Wednesday
    1, // Thursday
    4, // Friday
    0  // Saturday
};

// Map of the Chaldean order numbers to the planets and abbreviations
static const planet_names_t planet_names[] = {
    {"Satur", "SA"},
    {"Jupit", "JU"},
    {"Mars ", "MA"},
    {"Sun  ", "SU"},
    {"Venus", "VE"},
    {"Mercu", "ME"},
    {"Moon ", "MO"}};

static inline uint32_t _unix(watch_date_time_t t) { return watch_utility_date_time_to_unix_time(t, 0); }
static inline watch_date_time_t _from_unix(uint32_t ts) { return watch_utility_date_time_from_unix_time(ts, 0); }
static inline watch_date_time_t _midnight_of(watch_date_time_t t)
{
    t.unit.hour = t.unit.minute = t.unit.second = 0;
    return t;
}
static inline watch_date_time_t _add_days(watch_date_time_t day_midnight, int d)
{
    return _from_unix(_unix(day_midnight) + 86400u * (uint32_t)(d >= 0 ? d : -d) * (d >= 0 ? 1 : -1));
}

static lat_lon_settings_t _planetary_hour_face_struct_from_latlon(int16_t val)
{
    lat_lon_settings_t retval;

    retval.sign = val < 0;
    val = abs(val);
    retval.hundredths = val % 10;
    val /= 10;
    retval.tenths = val % 10;
    val /= 10;
    retval.ones = val % 10;
    val /= 10;
    retval.tens = val % 10;
    val /= 10;
    retval.hundreds = val % 10;

    return retval;
}

static void _planetary_hour_set_expiration(planetary_hour_state_t *state, watch_date_time_t next_hours_offset)
{
    uint32_t timestamp = _unix(next_hours_offset);
    state->hour_offset_expires = _from_unix(timestamp + 60);
}

// Divides [period_start, period_end) into 12 equal planetary hours.
// Returns which subdivision (0-11) contains target_time,
// and writes the start/end of that subdivision to the out-params.
static int _find_planetary_hour_in_period(watch_date_time_t period_start,
                                          watch_date_time_t period_end,
                                          watch_date_time_t target_time,
                                          watch_date_time_t *hour_start_out,
                                          watch_date_time_t *hour_end_out)
{
    uint32_t ps = _unix(period_start);
    uint32_t pe = _unix(period_end);
    uint32_t tt = _unix(target_time);

    double hour_duration = (double)(pe - ps) / 12.0;
    int index = (int)floor((double)(tt - ps) / hour_duration);

    if (index < 0) index = 0;
    if (index > 11) index = 11;

    *hour_start_out = _from_unix(ps + (uint32_t)(index * hour_duration));
    *hour_end_out   = _from_unix(ps + (uint32_t)((index + 1) * hour_duration));

    return index;
}

// this will return an entry from the planet_names map
static planet_names_t planetary_ruler_from_base_and_time(watch_date_time_t base_sunrise_local,
                                                         int time_since_sunrise)
{
    // 1. Calculate the day of the week from base_sunrise_local
    int year = base_sunrise_local.unit.year + WATCH_RTC_REFERENCE_YEAR;
    int month = base_sunrise_local.unit.month;
    int day = base_sunrise_local.unit.day;

    // Adjust for Zeller's Congruence if the month is January or February
    if (month < 3)
    {
        month += 12;
        year -= 1;
    }
    uint8_t day_of_week = (day + (2 * month) + (3 * (month + 1) / 5) + year + (year / 4) - (year / 100) + (year / 400) + 1) % 7;

    // 3. Get the ruler of the day from the day of the week
    uint8_t ruler_of_day_index = week_days_to_chaldean_order[day_of_week];

    // 4. Calculate the planetary ruler of the hour
    int ruler_index = (ruler_of_day_index + time_since_sunrise) % 7;

    // Return the corresponding planet from the planet_names map
    return planet_names[ruler_index];
}

// --- Small time helpers (same rounding/carry style as your sunrise/sunset face) ---
static watch_date_time_t _local_decimal_hours_to_dt(watch_date_time_t day_local, double local_hours_dec) {
    watch_date_time_t t = day_local;

    // Extract the integer part of local_hours_dec as the hour
    int hour = (int)floor(local_hours_dec);
    double fractional_hour = local_hours_dec - hour;

    // Adjust for day rollover if the hour is negative or exceeds 23
    if (hour < 0) {
        hour += 24;
        uint32_t ts = _unix(day_local);
        ts -= 86400; // Subtract one day
        t = _from_unix(ts);
    } else if (hour >= 24) {
        hour -= 24;
        uint32_t ts = _unix(day_local);
        ts += 86400; // Add one day
        t = _from_unix(ts);
    }

    t.unit.hour = (uint8_t)hour;

    // Calculate the fractional part for minutes and seconds
    double minutes = 60.0 * fractional_hour;
    double seconds = 60.0 * fmod(minutes, 1.0);

    printf("Debug: Converting decimal hours to date-time. Decimal hours: %.2f, Hour: %d, Minutes: %.2f, Seconds: %.2f\n",
           local_hours_dec, t.unit.hour, minutes, seconds);

    t.unit.minute = (seconds < 30.0) ? (uint8_t)floor(minutes) : (uint8_t)ceil(minutes);

    if (t.unit.minute == 60) {
        t.unit.minute = 0;
        t.unit.hour = (uint8_t)((t.unit.hour + 1) % 24);
        if (t.unit.hour == 0) {
            uint32_t ts = _unix(t);
            ts += 86400;
            t = _from_unix(ts);
        }
    }

    printf("Debug: Converted time: %02d:%02d\n", t.unit.hour, t.unit.minute);

    return t;
}

static bool _compute_local_sun_times(watch_date_time_t day_local,
                                     double lon, double lat, double hours_from_utc,
                                     watch_date_time_t *sunrise_local,
                                     watch_date_time_t *sunset_local)
{
    double rise_utc_dec, set_utc_dec;
    uint8_t result = sun_rise_set(day_local.unit.year + WATCH_RTC_REFERENCE_YEAR,
                                  day_local.unit.month, day_local.unit.day,
                                  lon, lat, &rise_utc_dec, &set_utc_dec);

    printf("Debug: Computing sun times for date: %04d-%02d-%02d, lon: %.2f, lat: %.2f, UTC offset: %.2f\n",
           day_local.unit.year + WATCH_RTC_REFERENCE_YEAR, day_local.unit.month, day_local.unit.day, lon, lat, hours_from_utc);
    printf("Debug: Raw sunrise (UTC): %.2f, Raw sunset (UTC): %.2f\n", rise_utc_dec, set_utc_dec);

    if (result != 0)
    {
        printf("Debug: Error in sun_rise_set calculation, result: %d\n", result);
        return false; // polar day/night or error
    }

    *sunrise_local = _local_decimal_hours_to_dt(day_local, rise_utc_dec + hours_from_utc);
    *sunset_local = _local_decimal_hours_to_dt(day_local, set_utc_dec + hours_from_utc);

    printf("Debug: Local sunrise: %02d:%02d, Local sunset: %02d:%02d\n",
           sunrise_local->unit.hour, sunrise_local->unit.minute,
           sunset_local->unit.hour, sunset_local->unit.minute);

    return true;
}


static void _planetary_hour_face_advance_digit(planetary_hour_state_t *state)
{
    state->location_state.location_changed = true;
    if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM)
    {
        switch (state->location_state.page)
        {
        case 1: // latitude
            switch (state->location_state.active_digit)
            {
            case 0: // tens
                state->location_state.working_latitude.tens = (state->location_state.working_latitude.tens + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                {
                    // prevent latitude from going over ±90.
                    // TODO: perform these checks when advancing the digit?
                    state->location_state.working_latitude.ones = 0;
                    state->location_state.working_latitude.tenths = 0;
                    state->location_state.working_latitude.hundredths = 0;
                }
                break;
            case 1:
                state->location_state.working_latitude.ones = (state->location_state.working_latitude.ones + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                    state->location_state.working_latitude.ones = 0;
                break;
            case 2:
                state->location_state.working_latitude.tenths = (state->location_state.working_latitude.tenths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                    state->location_state.working_latitude.tenths = 0;
                break;
            case 3:
                state->location_state.working_latitude.hundredths = (state->location_state.working_latitude.hundredths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                    state->location_state.working_latitude.hundredths = 0;
                break;
            case 4:
                state->location_state.working_latitude.sign++;
                break;
            }
            break;
        case 2: // longitude
            switch (state->location_state.active_digit)
            {
            case 0:
                // Increase tens and handle carry-over to hundreds
                state->location_state.working_longitude.tens++;
                if (state->location_state.working_longitude.tens >= 10)
                {
                    state->location_state.working_longitude.tens = 0;
                    state->location_state.working_longitude.hundreds++;
                }

                // Reset if we've gone over ±180
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                {
                    state->location_state.working_longitude.hundreds = 0;
                    state->location_state.working_longitude.tens = 0;
                    state->location_state.working_longitude.ones = 0;
                    state->location_state.working_longitude.tenths = 0;
                    state->location_state.working_longitude.hundredths = 0;
                }
                break;
            case 1:
                state->location_state.working_longitude.ones = (state->location_state.working_longitude.ones + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                    state->location_state.working_longitude.ones = 0;
                break;
            case 2:
                state->location_state.working_longitude.tenths = (state->location_state.working_longitude.tenths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                    state->location_state.working_longitude.tenths = 0;
                break;
            case 3:
                state->location_state.working_longitude.hundredths = (state->location_state.working_longitude.hundredths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                    state->location_state.working_longitude.hundredths = 0;
                break;
            case 4:
                state->location_state.working_longitude.sign++;
                break;
            }
            break;
        }
    }
    else
    {
        switch (state->location_state.page)
        {
        case 1: // latitude
            switch (state->location_state.active_digit)
            {
            case 0:
                state->location_state.working_latitude.sign++;
                break;
            case 1:
                // we skip this digit
                break;
            case 2:
                state->location_state.working_latitude.tens = (state->location_state.working_latitude.tens + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                {
                    // prevent latitude from going over ±90.
                    // TODO: perform these checks when advancing the digit?
                    state->location_state.working_latitude.ones = 0;
                    state->location_state.working_latitude.tenths = 0;
                    state->location_state.working_latitude.hundredths = 0;
                }
                break;
            case 3:
                state->location_state.working_latitude.ones = (state->location_state.working_latitude.ones + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                    state->location_state.working_latitude.ones = 0;
                break;
            case 4:
                state->location_state.working_latitude.tenths = (state->location_state.working_latitude.tenths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                    state->location_state.working_latitude.tenths = 0;
                break;
            case 5:
                state->location_state.working_latitude.hundredths = (state->location_state.working_latitude.hundredths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000)
                    state->location_state.working_latitude.hundredths = 0;
                break;
            }
            break;
        case 2: // longitude
            switch (state->location_state.active_digit)
            {
            case 0:
                state->location_state.working_longitude.sign++;
                break;
            case 1:
                state->location_state.working_longitude.hundreds = (state->location_state.working_longitude.hundreds + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                {
                    // prevent longitude from going over ±180
                    state->location_state.working_longitude.tens = 8;
                    state->location_state.working_longitude.ones = 0;
                    state->location_state.working_longitude.tenths = 0;
                    state->location_state.working_longitude.hundredths = 0;
                }
                break;
            case 2:
                state->location_state.working_longitude.tens = (state->location_state.working_longitude.tens + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                    state->location_state.working_longitude.tens = 0;
                break;
            case 3:
                state->location_state.working_longitude.ones = (state->location_state.working_longitude.ones + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                    state->location_state.working_longitude.ones = 0;
                break;
            case 4:
                state->location_state.working_longitude.tenths = (state->location_state.working_longitude.tenths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                    state->location_state.working_longitude.tenths = 0;
                break;
            case 5:
                state->location_state.working_longitude.hundredths = (state->location_state.working_longitude.hundredths + 1) % 10;
                if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000)
                    state->location_state.working_longitude.hundredths = 0;
                break;
            }
            break;
        }
    }
}

// --------------- MAIN: Planetary Hour Face (with hour_offset) -----------------
static void _planetary_hour_face_update(planetary_hour_state_t *state)
{
    char buf[14];
    movement_location_t movement_location;
    if (state->longLatToUse == 0 || _location_count <= 1)
        movement_location = load_location_from_filesystem();
    else
    {
        movement_location.bit.latitude = longLatPresets[state->longLatToUse].latitude;
        movement_location.bit.longitude = longLatPresets[state->longLatToUse].longitude;
    }

    // error out if no location is set on the watch or presets
    if (movement_location.reg == 0)
    {
        watch_display_text_with_fallback(WATCH_POSITION_TOP, "PHour ", "PH");
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "No LOC", "No Loc");
        return;
    }

    // get the current time and location
    watch_date_time_t now_local = movement_get_local_date_time();
    int16_t lat_centi = (int16_t)movement_location.bit.latitude;
    int16_t lon_centi = (int16_t)movement_location.bit.longitude;
    double lat = (double)lat_centi / 100.0;
    double lon = (double)lon_centi / 100.0;
    double hours_from_utc = ((double)movement_get_current_timezone_offset()) / 3600.0;

    // Find the target hour start by advancing hour_offset steps from "current planetary hour"
    uint32_t unix_time = _unix(now_local);
    unix_time += state->hour_offset * 3600;

    watch_date_time_t target_time = _from_unix(unix_time);

    // // Find the start of the current hour
    // unix_time -= (unix_time % 3600);

    // ----- Planetary base selection (your rule; anchored to "now") -----

    watch_date_time_t target0 = _midnight_of(target_time);
    watch_date_time_t sr_target_day, ss_target_day;
    if (!_compute_local_sun_times(target0, lon, lat, hours_from_utc, &sr_target_day, &ss_target_day))
    {
        watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "PHour", "PH");
        watch_display_text(WATCH_POSITION_BOTTOM, "None  ");
        return;
    }


    watch_date_time_t period_start, period_end;
    bool is_night = false;
    watch_date_time_t base_day = target0; // the day whose ruler we use

    uint32_t tt = _unix(target_time);

    if (tt < _unix(sr_target_day)) {
        // Before sunrise: still in yesterday's night period
        watch_date_time_t sr_prev_day, ss_prev_day;
        watch_date_time_t target_neg1 = _add_days(target0, -1);
        _compute_local_sun_times(target_neg1, lon, lat, hours_from_utc, &sr_prev_day, &ss_prev_day);
        period_start = ss_prev_day;
        period_end = sr_target_day;
        is_night = true;
        base_day = target_neg1; // yesterday's ruler
    } else if (tt >= _unix(ss_target_day)) {
        // After sunset: tonight's period
        watch_date_time_t sr_next_day, ss_next_day;
        watch_date_time_t target1 = _add_days(target0, 1);
        _compute_local_sun_times(target1, lon, lat, hours_from_utc, &sr_next_day, &ss_next_day);
        period_start = ss_target_day;
        period_end = sr_next_day;
        is_night = true;
        base_day = target0; // today's ruler
    } else {
        // Between sunrise and sunset: daytime
        period_start = sr_target_day;
        period_end = ss_target_day;
        is_night = false;
        base_day = target0; // today's ruler
    }

    // Divide the period into 12 equal planetary hours
    watch_date_time_t hour_start, hour_end;
    int hour_index = _find_planetary_hour_in_period(period_start, period_end, target_time,
                                                     &hour_start, &hour_end);

    // Night hours are hours 13-24 of the planetary day (offset by 12)
    int chaldean_offset = hour_index + (is_night ? 12 : 0);

    planet_names_t ruler = planetary_ruler_from_base_and_time(base_day, chaldean_offset);
    watch_date_time_t display_time = hour_start;

    _planetary_hour_set_expiration(state, hour_end);

    printf("Period: %02d:%02d-%02d:%02d, Hour: %d, Night: %d, Ruler: %s, Display: %02d:%02d\n",
           period_start.unit.hour, period_start.unit.minute,
           period_end.unit.hour, period_end.unit.minute,
           hour_index, is_night, ruler.name,
           display_time.unit.hour, display_time.unit.minute);

    // ---- DISPLAY ----
    watch_set_colon();
    if (movement_clock_mode_24h())
        watch_set_indicator(WATCH_INDICATOR_24H);
    if (!movement_clock_mode_24h())
    {
        watch_date_time_t disp = display_time;
        if (watch_utility_convert_to_12_hour(&disp))
            watch_set_indicator(WATCH_INDICATOR_PM);
        else
            watch_clear_indicator(WATCH_INDICATOR_PM);
    }
    else
    {
        watch_clear_indicator(WATCH_INDICATOR_PM);
    }

    watch_display_text_with_fallback(WATCH_POSITION_TOP, ruler.name, ruler.abbreviation);

    watch_date_time_t disp = display_time;
    if (!movement_clock_mode_24h())
        (void)watch_utility_convert_to_12_hour(&disp);
    sprintf(buf, "%2d%02d%2d", disp.unit.hour, disp.unit.minute, disp.unit.day);
    watch_display_text(WATCH_POSITION_BOTTOM, buf);
}

// Function to set up the planetary face, allocating memory for the context
void planetary_hour_face_setup(__attribute__((unused)) uint8_t watch_face_index, void **context_ptr)
{
    if (*context_ptr == NULL)
    {
        *context_ptr = malloc(sizeof(planetary_hour_state_t));
        memset(*context_ptr, 0, sizeof(planetary_hour_state_t));
    }
}

// Function to activate the planetary face, initializing planetary hour and zodiac sign
void planetary_hour_face_activate(void *context)
{
    planetary_hour_state_t *state = (planetary_hour_state_t *)context;
    // Initialize the location_state
    state->hour_offset = 0;
    state->longLatToUse = 0;
    state->hour_offset_expires = movement_get_local_date_time(); // force immediate update

    movement_location_t movement_location = load_location_from_filesystem();
    state->location_state.working_latitude = _planetary_hour_face_struct_from_latlon(movement_location.bit.latitude);
    state->location_state.working_longitude = _planetary_hour_face_struct_from_latlon(movement_location.bit.longitude);
    state->location_state.page = 0;
    state->location_state.active_digit = 0;
    state->location_state.location_changed = false;
}

// Main loop for the planetary face, handling events and updating the display
bool planetary_hour_face_loop(movement_event_t event, void *context)
{
    planetary_hour_state_t *state = (planetary_hour_state_t *)context;

    // Check if the context is null to avoid dereferencing a null pointer
    if (state == NULL)
    {
        watch_display_text(WATCH_POSITION_TOP, "Error");
        watch_display_text(WATCH_POSITION_BOTTOM, "Error");
        return false;
    }

    switch (event.event_type)
    {
    case EVENT_ACTIVATE:
        _planetary_hour_face_update(state);
        break;

    case EVENT_LOW_ENERGY_UPDATE:
    case EVENT_TICK:
        if (state->location_state.page == 0)
        {
            // if entering low energy mode, start tick animation
            if (event.event_type == EVENT_LOW_ENERGY_UPDATE && !watch_sleep_animation_is_running())
                watch_start_sleep_animation(1000);
            // check if we need to update the display
            watch_date_time_t date_time = movement_get_local_date_time();
            if (date_time.reg >= state->hour_offset_expires.reg)
            {
                _planetary_hour_face_update(state);
            }
        }
        else
        {
            _update_location_settings_display(event, &state->location_state);
        }
        break;

    case EVENT_ALARM_LONG_PRESS:
        if (state->location_state.page == 0)
        {
            if (state->longLatToUse != 0)
            {
                state->longLatToUse = 0;
                _planetary_hour_face_update(state);
                break;
            }
            state->location_state.page++;
            state->location_state.active_digit = 0;
            watch_clear_display();
            movement_request_tick_frequency(4);
            _update_location_settings_display(event, &state->location_state);
        }
        else
        {
            state->location_state.active_digit = 0;
            state->location_state.page = 0;
            _update_location_register(&state->location_state);
            _planetary_hour_face_update(state);
        }
        break;

    case EVENT_ALARM_BUTTON_UP:
        if (state->location_state.page)
        {
            _planetary_hour_face_advance_digit(state);
            _update_location_settings_display(event, &state->location_state);
        }
        else
        {
            state->hour_offset++;
            _planetary_hour_face_update(state);
        }
        break;

    case EVENT_TIMEOUT:
        movement_move_to_face(0);
        break;

    case EVENT_LIGHT_BUTTON_DOWN:
    case EVENT_ALARM_BUTTON_DOWN:
        break;

    case EVENT_LIGHT_LONG_PRESS:
        movement_illuminate_led();
        break;

    case EVENT_LIGHT_BUTTON_UP:
        if (state->location_state.page == 0 && _location_count > 1)
        {
            state->longLatToUse = (state->longLatToUse + 1) % _location_count;
            _planetary_hour_face_update(state);
            break;
        }
        if (state->location_state.page)
        {
            if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM)
            {
                state->location_state.active_digit++;
                if (state->location_state.active_digit > 4)
                {
                    state->location_state.active_digit = 0;
                    state->location_state.page = (state->location_state.page + 1) % 3;
                    _update_location_register(&state->location_state);
                }
            }
            else
            {
                state->location_state.active_digit++;
                if (state->location_state.page == 1 && state->location_state.active_digit == 1)
                    state->location_state.active_digit++; // max latitude is +- 90, no hundreds place
                if (state->location_state.active_digit > 5)
                {
                    state->location_state.active_digit = 0;
                    state->location_state.page = (state->location_state.page + 1) % 3;
                    _update_location_register(&state->location_state);
                }
            }
            _update_location_settings_display(event, &state->location_state);
        } 
        if (state->location_state.page == 0)
        {
            movement_request_tick_frequency(1);
            state->hour_offset--; // go back one hour
            _planetary_hour_face_update(state);
        }
        break;

    default:
        return movement_default_loop_handler(event); // Handle other events with default handler
    }
    return true;
}

// Function to release resources when the planetary face is no longer active
void planetary_hour_face_resign(void *context)
{
    planetary_hour_state_t *state = (planetary_hour_state_t *)context;
    state->location_state.page = 0;
    state->location_state.active_digit = 0;
    state->hour_offset = 0;
    _update_location_register(&state->location_state);
}

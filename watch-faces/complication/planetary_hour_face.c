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
#include "planetary.h"
#include "watch.h"
#include "watch_utility.h"
#include <string.h>
#include <stdlib.h>
#include "filesystem.h"
#include <stdio.h> // For sprintf/snprintf

static const uint8_t _location_count = sizeof(longLatPresets) / sizeof(long_lat_presets_t);

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

    double lat = (double)(int16_t)movement_location.bit.latitude / 100.0;
    double lon = (double)(int16_t)movement_location.bit.longitude / 100.0;
    int32_t utc_offset = movement_get_current_timezone_offset();

    // The planetary hour containing the time hour_offset clock hours from now.
    int64_t target = (int64_t)movement_get_utc_timestamp() + (int64_t)state->hour_offset * 3600;
    planetary_hour_t hour;
    if (!planetary_hour_at(target, utc_offset, lat, lon, &hour))
    {
        watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "PHour", "PH");
        watch_display_text(WATCH_POSITION_BOTTOM, "None  ");
        return;
    }

    planetary_planet_t ruler = planetary_planets[hour.ruler];
    watch_date_time_t display_time = watch_utility_date_time_from_unix_time((uint32_t)hour.start, utc_offset);
    state->hour_offset_expires = watch_utility_date_time_from_unix_time((uint32_t)(hour.end + 60), utc_offset);

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
    if (watch_sleep_animation_is_running()) watch_stop_sleep_animation();

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

// Leaving the location settings: back to the current hour at the normal 1 Hz tick.
static void _planetary_hour_face_show_current_hour(planetary_hour_state_t *state)
{
    state->hour_offset = 0;
    movement_request_tick_frequency(1);
    _planetary_hour_face_update(state);
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
            _planetary_hour_face_show_current_hour(state);
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
            if (state->location_state.page == 0)
                _planetary_hour_face_show_current_hour(state); // left settings
            else
                _update_location_settings_display(event, &state->location_state);
        }
        else
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

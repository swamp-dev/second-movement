/*
 * MIT License
 *
 * Copyright (c) 2022 Joey Castillo
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
 * Sunrise/sunset calculations are public domain code by Paul Schlyter, December 1992
 *
 */

#include <stdlib.h>
#include <string.h>
#include "sunrise_sunset_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_common_display.h"
#include "filesystem.h"
#include "sun_events.h"

#if __EMSCRIPTEN__
#include <emscripten.h>
#endif

static const uint8_t _location_count = sizeof(longLatPresets) / sizeof(long_lat_presets_t);

static void _sunrise_sunset_face_update(sunrise_sunset_face_state_t *state) {
    char buf[14];
    movement_location_t movement_location;
    if (state->longLatToUse == 0 || _location_count <= 1)
        movement_location = load_location_from_filesystem();
    else{
        movement_location.bit.latitude = longLatPresets[state->longLatToUse].latitude;
        movement_location.bit.longitude = longLatPresets[state->longLatToUse].longitude;
    }

    if (movement_location.reg == 0) {
        // Activate, the location editor and the preset switch are the only ways
        // the location can change, and all of them call this function, so ticks
        // need not re-read it.
        state->refresh_at = UINT32_MAX;
        watch_display_text_with_fallback(WATCH_POSITION_TOP, "Sunri", "rI");
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "No LOC", "No Loc");
        return;
    }

    double lat = (double)(int16_t)movement_location.bit.latitude / 100.0;
    double lon = (double)(int16_t)movement_location.bit.longitude / 100.0;

    // Events are Unix times, so one that falls on another local day than the
    // date it was computed for still shows its own day and time (#47).
    uint32_t now = movement_get_utc_timestamp();
    sun_event_t next = sun_events_upcoming(now, 0, lat, lon);
    sun_event_t shown = state->rise_index ? sun_events_upcoming(now, state->rise_index, lat, lon) : next;
    // Keep showing the next event for a minute after it happens.
    state->refresh_at = (uint32_t)(next.t + 60);

    // The offset in force on the event's date, which may differ from today's
    // across a daylight saving change.
    watch_date_time_t disp = watch_utility_date_time_from_unix_time((uint32_t)shown.t, movement_get_current_timezone_offset());
    int32_t offset = movement_get_timezone_offset_for_date(disp);
    // Shown to the nearest minute.
    disp = watch_utility_date_time_from_unix_time((uint32_t)(shown.t + 30), offset);

    bool is_rise = shown.kind == SUN_EVENT_RISE || shown.kind == SUN_EVENT_NO_RISE;
    if (is_rise) watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "RIS", "rI");
    else watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "SET", "SE");

    if (shown.kind == SUN_EVENT_NO_RISE || shown.kind == SUN_EVENT_NO_SET) {
        watch_clear_colon();
        watch_clear_indicator(WATCH_INDICATOR_PM);
        watch_clear_indicator(WATCH_INDICATOR_24H);
        sprintf(buf, "%2d", disp.unit.day);
        watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
        watch_display_text(WATCH_POSITION_BOTTOM, "None  ");
        return;
    }

    watch_set_colon();
    if (movement_clock_mode_24h()) {
        watch_set_indicator(WATCH_INDICATOR_24H);
        watch_clear_indicator(WATCH_INDICATOR_PM);
    } else {
        watch_clear_indicator(WATCH_INDICATOR_24H);
        // Converting changes only the hour, so the day below is still right.
        if (watch_utility_convert_to_12_hour(&disp)) watch_set_indicator(WATCH_INDICATOR_PM);
        else watch_clear_indicator(WATCH_INDICATOR_PM);
    }
    sprintf(buf, "%2d", disp.unit.day);
    watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
    sprintf(buf, "%2d%02d%2s", disp.unit.hour, disp.unit.minute, longLatPresets[state->longLatToUse].name);
    watch_display_text(WATCH_POSITION_BOTTOM, buf);
}


static lat_lon_settings_t _sunrise_sunset_face_struct_from_latlon(int16_t val) {
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

static void _sunrise_sunset_face_advance_digit(sunrise_sunset_face_state_t *state) {
    state->location_state.location_changed = true;
    if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM) {
        switch (state->location_state.page) {
            case 1: // latitude
                switch (state->location_state.active_digit) {
                    case 0: // tens
                        state->location_state.working_latitude.tens = (state->location_state.working_latitude.tens + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) {
                            // prevent latitude from going over ±90.
                            // TODO: perform these checks when advancing the digit?
                            state->location_state.working_latitude.ones = 0;
                            state->location_state.working_latitude.tenths = 0;
                            state->location_state.working_latitude.hundredths = 0;
                        }
                        break;
                    case 1:
                        state->location_state.working_latitude.ones = (state->location_state.working_latitude.ones + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) state->location_state.working_latitude.ones = 0;
                        break;
                    case 2:
                        state->location_state.working_latitude.tenths = (state->location_state.working_latitude.tenths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) state->location_state.working_latitude.tenths = 0;
                        break;
                    case 3:
                        state->location_state.working_latitude.hundredths = (state->location_state.working_latitude.hundredths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) state->location_state.working_latitude.hundredths = 0;
                        break;
                    case 4:
                        state->location_state.working_latitude.sign++;
                        break;
                }
                break;
            case 2: // longitude
                switch (state->location_state.active_digit) {
                    case 0:
                        // Increase tens and handle carry-over to hundreds
                        state->location_state.working_longitude.tens++;
                        if (state->location_state.working_longitude.tens >= 10) {
                            state->location_state.working_longitude.tens = 0;
                            state->location_state.working_longitude.hundreds++;
                        }

                        // Reset if we've gone over ±180
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) {
                            state->location_state.working_longitude.hundreds = 0;
                            state->location_state.working_longitude.tens = 0;
                            state->location_state.working_longitude.ones = 0;
                            state->location_state.working_longitude.tenths = 0;
                            state->location_state.working_longitude.hundredths = 0;
                        }
                        break;
                    case 1:
                        state->location_state.working_longitude.ones = (state->location_state.working_longitude.ones + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) state->location_state.working_longitude.ones = 0;
                        break;
                    case 2:
                        state->location_state.working_longitude.tenths = (state->location_state.working_longitude.tenths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) state->location_state.working_longitude.tenths = 0;
                        break;
                    case 3:
                        state->location_state.working_longitude.hundredths = (state->location_state.working_longitude.hundredths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) state->location_state.working_longitude.hundredths = 0;
                        break;
                    case 4:
                        state->location_state.working_longitude.sign++;
                        break;
                }
                break;
        }
    } else {
        switch (state->location_state.page) {
            case 1: // latitude
                switch (state->location_state.active_digit) {
                    case 0:
                        state->location_state.working_latitude.sign++;
                        break;
                    case 1:
                        // we skip this digit
                        break;
                    case 2:
                        state->location_state.working_latitude.tens = (state->location_state.working_latitude.tens + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) {
                            // prevent latitude from going over ±90.
                            // TODO: perform these checks when advancing the digit?
                            state->location_state.working_latitude.ones = 0;
                            state->location_state.working_latitude.tenths = 0;
                            state->location_state.working_latitude.hundredths = 0;
                        }
                        break;
                    case 3:
                        state->location_state.working_latitude.ones = (state->location_state.working_latitude.ones + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) state->location_state.working_latitude.ones = 0;
                        break;
                    case 4:
                        state->location_state.working_latitude.tenths = (state->location_state.working_latitude.tenths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) state->location_state.working_latitude.tenths = 0;
                        break;
                    case 5:
                        state->location_state.working_latitude.hundredths = (state->location_state.working_latitude.hundredths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_latitude)) > 9000) state->location_state.working_latitude.hundredths = 0;
                        break;
                }
                break;
            case 2: // longitude
                switch (state->location_state.active_digit) {
                    case 0:
                        state->location_state.working_longitude.sign++;
                        break;
                    case 1:
                        state->location_state.working_longitude.hundreds = (state->location_state.working_longitude.hundreds + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) {
                            // prevent longitude from going over ±180
                            state->location_state.working_longitude.tens = 8;
                            state->location_state.working_longitude.ones = 0;
                            state->location_state.working_longitude.tenths = 0;
                            state->location_state.working_longitude.hundredths = 0;
                        }
                        break;
                    case 2:
                        state->location_state.working_longitude.tens = (state->location_state.working_longitude.tens + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) state->location_state.working_longitude.tens = 0;
                        break;
                    case 3:
                        state->location_state.working_longitude.ones = (state->location_state.working_longitude.ones + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) state->location_state.working_longitude.ones = 0;
                        break;
                    case 4:
                        state->location_state.working_longitude.tenths = (state->location_state.working_longitude.tenths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) state->location_state.working_longitude.tenths = 0;
                        break;
                    case 5:
                        state->location_state.working_longitude.hundredths = (state->location_state.working_longitude.hundredths + 1) % 10;
                        if (abs(_latlon_from_struct(state->location_state.working_longitude)) > 18000) state->location_state.working_longitude.hundredths = 0;
                        break;
                }
                break;
        }
    }
}

void sunrise_sunset_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(sunrise_sunset_face_state_t));
        if (*context_ptr == NULL) {
            return;
        }
        memset(*context_ptr, 0, sizeof(sunrise_sunset_face_state_t));
    }
}

void sunrise_sunset_face_activate(void *context) {
    if (watch_sleep_animation_is_running()) watch_stop_sleep_animation();

#if __EMSCRIPTEN__
    int16_t browser_lat = EM_ASM_INT({
        return lat;
    });
    int16_t browser_lon = EM_ASM_INT({
        return lon;
    });
    if ((watch_get_backup_data(1) == 0) && (browser_lat || browser_lon)) {
        movement_location_t browser_loc;
        browser_loc.bit.latitude = browser_lat;
        browser_loc.bit.longitude = browser_lon;
        watch_store_backup_data(browser_loc.reg, 1);
    }
#endif

    sunrise_sunset_face_state_t *state = (sunrise_sunset_face_state_t *)context;
    movement_location_t movement_location = load_location_from_filesystem();
    state->location_state.working_latitude = _sunrise_sunset_face_struct_from_latlon(movement_location.bit.latitude);
    state->location_state.working_longitude = _sunrise_sunset_face_struct_from_latlon(movement_location.bit.longitude);
    state->location_state.page = 0;
    state->location_state.active_digit = 0;
    state->location_state.location_changed = false;
}

bool sunrise_sunset_face_loop(movement_event_t event, void *context) {
    sunrise_sunset_face_state_t *state = (sunrise_sunset_face_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _sunrise_sunset_face_update(state);
            break;
        case EVENT_LOW_ENERGY_UPDATE:
        case EVENT_TICK:
            if (state->location_state.page == 0) {
                // if entering low energy mode, start tick animation
                if (event.event_type == EVENT_LOW_ENERGY_UPDATE && !watch_sleep_animation_is_running()) watch_start_sleep_animation(1000);
                // check if we need to update the display
                if (movement_get_utc_timestamp() >= state->refresh_at) {
                    // and on the off chance that this happened before EVENT_TIMEOUT snapped us back to rise/set 0, go back now
                    state->rise_index = 0;
                    _sunrise_sunset_face_update(state);
                }
            } else {
                _update_location_settings_display(event, &state->location_state);
            }
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            if (state->location_state.page) {
                if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM) {
                    state->location_state.active_digit++;
                    if (state->location_state.active_digit > 4) {
                        state->location_state.active_digit = 0;
                        state->location_state.page = (state->location_state.page + 1) % 3;
                        _update_location_register(&state->location_state);
                    }
                } else {
                    state->location_state.active_digit++;
                    if (state->location_state.page == 1 && state->location_state.active_digit == 1) state->location_state.active_digit++; // max latitude is +- 90, no hundreds place
                    if (state->location_state.active_digit > 5) {
                        state->location_state.active_digit = 0;
                        state->location_state.page = (state->location_state.page + 1) % 3;
                        _update_location_register(&state->location_state);
                    }
                }
                _update_location_settings_display(event, &state->location_state);
                _update_location_settings_display(event, &state->location_state);
            } else if (_location_count <= 1) {
                movement_illuminate_led();
            }
            if (state->location_state.page == 0) {
                movement_request_tick_frequency(1);
                _sunrise_sunset_face_update(state);
            }
            break;
        case EVENT_LIGHT_LONG_PRESS:
            if (_location_count <= 1) break;
            else if (!state->location_state.page) movement_illuminate_led();
            break;
        case EVENT_LIGHT_BUTTON_UP:
            if (state->location_state.page == 0 && _location_count > 1) {
                state->longLatToUse = (state->longLatToUse + 1) % _location_count;
                _sunrise_sunset_face_update(state);
            }
            break;
        case EVENT_ALARM_BUTTON_UP:
            if (state->location_state.page) {
                _sunrise_sunset_face_advance_digit(state);
                _update_location_settings_display(event, &state->location_state);
            } else {
                state->rise_index = (state->rise_index + 1) % 2;
                _sunrise_sunset_face_update(state);
            }
            break;
        case EVENT_ALARM_LONG_PRESS:
            if (state->location_state.page == 0) {
            if (state->longLatToUse != 0) {
                state->longLatToUse = 0;
                _sunrise_sunset_face_update(state);
                break;
            }
                state->location_state.page++;
                state->location_state.active_digit = 0;
                watch_clear_display();
                movement_request_tick_frequency(4);
                _update_location_settings_display(event, &state->location_state);
            }
            else {
                state->location_state.active_digit = 0;
                state->location_state.page = 0;
                _update_location_register(&state->location_state);
                _sunrise_sunset_face_update(state);
            }
            break;
        case EVENT_TIMEOUT:
            if (load_location_from_filesystem().reg == 0) {
                // if no location set, return home
                movement_move_to_face(0);
            } else if (state->location_state.page || state->rise_index) {
                // otherwise on timeout, exit settings mode and return to the next sunrise or sunset
                state->location_state.page = 0;
                state->rise_index = 0;
                movement_request_tick_frequency(1);
                _sunrise_sunset_face_update(state);
            }
            break;
        default:
            return movement_default_loop_handler(event);
    }

    return true;
}

void sunrise_sunset_face_resign(void *context) {
    sunrise_sunset_face_state_t *state = (sunrise_sunset_face_state_t *)context;
    state->location_state.page = 0;
    state->location_state.active_digit = 0;
    state->rise_index = 0;
    _update_location_register(&state->location_state);
}

#include "fake_lcd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "watch_common_display.h"

// The SLCD addresses COM 0-7 and SEG 0-63; watch_common_display.c writes
// COM 4 for the indicators a display lacks, so keep the whole range.
#define COMS 8
#define SEGS 64
#define MAX_POSITIONS 11
#define FIRST_CHAR 0x20
#define LAST_CHAR 0x7e

typedef bool pixels_t[COMS][SEGS];

// Defined in watch_common_display.c with no header declaration; the watch
// calls _watch_update_indicator_segments from watch_discover_lcd_type once the
// custom LCD is detected.
extern uint8_t IndicatorSegments[8];

static watch_lcd_type_t lcd_type = WATCH_LCD_TYPE_CLASSIC;
static pixels_t screen;
// Draws go to `active`. The decoder points it at a scratch buffer to see what
// a character would draw, and records every pixel the draw touches.
static bool (*active)[SEGS] = screen;
static bool (*touched)[SEGS];

static uint8_t classic_indicators[8];
static bool classic_indicators_saved;

// An address outside the SLCD would write past the buffer; a SEG past 63 would
// land silently in the next row, which ASan can't see. Stop the test instead.
static void check_address(uint8_t com, uint8_t seg) {
    if (com >= COMS || seg >= SEGS) {
        fprintf(stderr, "fake_lcd: pixel (%d,%d) is outside the display\n", com, seg);
        abort();
    }
}

void watch_set_pixel(uint8_t com, uint8_t seg) {
    check_address(com, seg);
    active[com][seg] = true;
    if (touched) touched[com][seg] = true;
}

void watch_clear_pixel(uint8_t com, uint8_t seg) {
    check_address(com, seg);
    active[com][seg] = false;
    if (touched) touched[com][seg] = true;
}

void watch_clear_display(void) {
    memset(active, 0, sizeof(pixels_t));
}

watch_lcd_type_t watch_get_lcd_type(void) {
    return lcd_type;
}

void fake_lcd_reset(watch_lcd_type_t type) {
    // IndicatorSegments starts in the classic layout and only ever switches
    // to custom, so keep the classic layout to switch back.
    if (!classic_indicators_saved) {
        memcpy(classic_indicators, IndicatorSegments, sizeof(classic_indicators));
        classic_indicators_saved = true;
    }
    lcd_type = type;
    memcpy(IndicatorSegments, classic_indicators, sizeof(classic_indicators));
    _watch_update_indicator_segments();
    memset(screen, 0, sizeof(screen));
}

uint8_t fake_lcd_num_positions(void) {
    return lcd_type == WATCH_LCD_TYPE_CUSTOM ? 11 : 10;
}

bool fake_lcd_pixel(uint8_t com, uint8_t seg) {
    return com < COMS && seg < SEGS && screen[com][seg];
}

static int type_index(void) {
    return lcd_type == WATCH_LCD_TYPE_CUSTOM ? 1 : 0;
}

// Draws character c at a position into a blank `out`, recording in `mark`
// every pixel the draw sets or clears.
static void draw_into(pixels_t out, pixels_t mark, char c, uint8_t position) {
    memset(out, 0, sizeof(pixels_t));
    memset(mark, 0, sizeof(pixels_t));
    active = out;
    touched = mark;
    watch_display_character((uint8_t)c, position);
    active = screen;
    touched = NULL;
}

static digit_mapping_t position_mapping(uint8_t position) {
    return lcd_type == WATCH_LCD_TYPE_CUSTOM ? Custom_LCD_Display_Mapping[position]
                                             : Classic_LCD_Display_Mapping[position];
}

bool fake_lcd_in_mapping(uint8_t position, uint8_t com, uint8_t seg) {
    if (position >= fake_lcd_num_positions()) return false;
    digit_mapping_t segmap = position_mapping(position);
    for (int i = 0; i < 8; i++) {
        if (segmap.segment[i].value == segment_does_not_exist) continue;
        if (segmap.segment[i].address.com == com && segmap.segment[i].address.seg == seg) return true;
    }
    return false;
}

// The pixels a position controls: every pixel some character's draw there
// sets or clears, including the extra segments classic positions 0 and 1 use,
// but not a segment in another position's mapping (#116 draws some of those).
static const bool (*owned_pixels(uint8_t position))[SEGS] {
    static pixels_t owned[2][MAX_POSITIONS];
    static bool computed[2][MAX_POSITIONS];
    int t = type_index();
    if (!computed[t][position]) {
        pixels_t out, mark;
        for (int c = FIRST_CHAR; c <= LAST_CHAR; c++) {
            draw_into(out, mark, (char)c, position);
            for (int com = 0; com < COMS; com++)
                for (int seg = 0; seg < SEGS; seg++)
                    if (mark[com][seg]) owned[t][position][com][seg] = true;
        }
        for (int other = 0; other < fake_lcd_num_positions(); other++) {
            if (other == position) continue;
            for (int com = 0; com < COMS; com++)
                for (int seg = 0; seg < SEGS; seg++)
                    if (fake_lcd_in_mapping(other, com, seg) && !fake_lcd_in_mapping(position, com, seg))
                        owned[t][position][com][seg] = false;
        }
        computed[t][position] = true;
    }
    return (const bool (*)[SEGS])owned[t][position];
}

// True if drawing c at the position gives the pattern its character-set entry
// describes, i.e. the display code didn't swap c for another character.
static bool drawn_as_itself(const pixels_t drawn, char c, uint8_t position) {
    digit_mapping_t segmap = position_mapping(position);
    uint8_t segdata = lcd_type == WATCH_LCD_TYPE_CUSTOM ? Custom_LCD_Character_Set[c - FIRST_CHAR]
                                                        : Classic_LCD_Character_Set[c - FIRST_CHAR];
    // Write the entry in segment order, as watch_display_character does, so a
    // segment address shared by two segments ends with the later one.
    pixels_t native;
    memset(native, 0, sizeof(native));
    for (int i = 0; i < 8; i++, segdata >>= 1) {
        if (segmap.segment[i].value == segment_does_not_exist) continue;
        native[segmap.segment[i].address.com][segmap.segment[i].address.seg] = segdata & 1;
    }
    for (int i = 0; i < 8; i++) {
        if (segmap.segment[i].value == segment_does_not_exist) continue;
        uint8_t com = segmap.segment[i].address.com, seg = segmap.segment[i].address.seg;
        if (drawn[com][seg] != native[com][seg]) return false;
    }
    return true;
}

// Candidates in the order a match is preferred: blank, digits, letters, then
// punctuation.
static const char *candidates(void) {
    static char order[LAST_CHAR - FIRST_CHAR + 2];
    if (!order[0]) {
        size_t n = 0;
        const char *first = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        for (const char *p = first; *p; p++) order[n++] = *p;
        for (int c = FIRST_CHAR; c <= LAST_CHAR; c++)
            if (!strchr(first, c)) order[n++] = (char)c;
        order[n] = 0;
    }
    return order;
}

char fake_lcd_char(uint8_t position) {
    if (position >= fake_lcd_num_positions()) return '?';
    const bool (*owned)[SEGS] = owned_pixels(position);
    // Preference: a letter or digit drawn as itself ('n' over 'M', which
    // classic draws as n), then any letter or digit ('7', which classic draws
    // with the '&' glyph in positions 4 and 6), then other characters.
    char best[4] = {0, 0, 0, 0};
    pixels_t out, mark;
    for (const char *p = candidates(); *p; p++) {
        draw_into(out, mark, *p, position);
        bool match = true;
        for (int com = 0; com < COMS && match; com++)
            for (int seg = 0; seg < SEGS && match; seg++)
                if (owned[com][seg] && out[com][seg] != screen[com][seg]) match = false;
        if (!match) continue;
        bool alnum = (*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == ' ';
        int rank = (alnum ? 0 : 2) + (drawn_as_itself(out, *p, position) ? 0 : 1);
        if (!best[rank]) best[rank] = *p;
    }
    for (int rank = 0; rank < 4; rank++)
        if (best[rank]) return best[rank];
    return '?';
}

// The positions a location fills, in the order watch_display_text and
// watch_display_text_with_fallback fill them. Returns how many.
static size_t location_positions(watch_position_t location, const uint8_t **positions) {
    static const uint8_t classic_top[] = {0, 1}, custom_top[] = {0, 1, 10, 2, 3};
    static const uint8_t custom_top_left[] = {0, 1, 10}, top_right[] = {2, 3};
    static const uint8_t bottom[] = {4, 5, 6, 7, 8, 9}, hours[] = {4, 5};
    static const uint8_t minutes[] = {6, 7}, seconds[] = {8, 9};
    static const uint8_t full[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bool custom = lcd_type == WATCH_LCD_TYPE_CUSTOM;

    switch (location) {
        case WATCH_POSITION_TOP:
            *positions = custom ? custom_top : classic_top;
            return custom ? 5 : 2;
        case WATCH_POSITION_TOP_LEFT:
            *positions = custom ? custom_top_left : classic_top;
            return custom ? 3 : 2;
        case WATCH_POSITION_TOP_RIGHT: *positions = top_right; return 2;
        case WATCH_POSITION_BOTTOM: *positions = bottom; return 6;
        case WATCH_POSITION_HOURS: *positions = hours; return 2;
        case WATCH_POSITION_MINUTES: *positions = minutes; return 2;
        case WATCH_POSITION_SECONDS: *positions = seconds; return 2;
        case WATCH_POSITION_FULL: *positions = full; return custom ? 11 : 10;
    }
    *positions = full;
    return 0;
}

const char *fake_lcd_text(watch_position_t location, char *buf, size_t size) {
    const uint8_t *positions;
    size_t count = location_positions(location, &positions), n = 0;
    if (size == 0) return buf;
    // The custom LCD's bottom line has a leading half-digit that can only show "1".
    if (location == WATCH_POSITION_BOTTOM && lcd_type == WATCH_LCD_TYPE_CUSTOM && screen[0][22] && n + 1 < size)
        buf[n++] = '1';
    for (size_t i = 0; i < count && n + 1 < size; i++) buf[n++] = fake_lcd_char(positions[i]);
    buf[n] = 0;
    return buf;
}

bool fake_lcd_shows(watch_position_t location, const char *text) {
    const uint8_t *positions;
    size_t width = location_positions(location, &positions);
    size_t len = strlen(text);
    // On custom, a 7-character bottom string starting with 1 uses the leading
    // half-digit: the rule in watch_display_text_with_fallback, kept in step.
    size_t max = width;
    if (location == WATCH_POSITION_BOTTOM && lcd_type == WATCH_LCD_TYPE_CUSTOM && len == width + 1 && text[0] == '1')
        max = width + 1;
    // Refuse what the display code can't draw rather than replay it: an empty
    // string or a character outside the font indexes before or past the
    // character set, and a too-long string past the position table (#60).
    static const char *names[] = {"FULL", "TOP", "TOP_LEFT", "TOP_RIGHT", "BOTTOM", "HOURS", "MINUTES", "SECONDS"};
    const char *name = (size_t)location < sizeof(names) / sizeof(names[0]) ? names[location] : "?";
    if (len == 0 || len > max) {
        fprintf(stderr, "fake_lcd_shows: refused %zu characters for %s, which holds %zu on the %s LCD\n", len, name, max,
                lcd_type == WATCH_LCD_TYPE_CUSTOM ? "custom" : "classic");
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (text[i] < FIRST_CHAR || text[i] > LAST_CHAR) {
            fprintf(stderr, "fake_lcd_shows: refused character 0x%02x for %s, outside the font\n",
                    (unsigned char)text[i], name);
            return false;
        }
    }
    // Pad to the full width, so "1" in HOURS means "1 ", not "1 and anything".
    char padded[MAX_POSITIONS + 2];
    memset(padded, ' ', sizeof(padded));
    memcpy(padded, text, len);
    padded[len > width ? len : width] = 0;

    pixels_t out, mark;
    memset(out, 0, sizeof(out));
    memset(mark, 0, sizeof(mark));
    active = out;
    touched = mark;
    watch_display_text_with_fallback(location, padded, padded);
    active = screen;
    touched = NULL;
    for (int com = 0; com < COMS; com++)
        for (int seg = 0; seg < SEGS; seg++)
            if (mark[com][seg] && out[com][seg] != screen[com][seg]) return false;
    return true;
}

bool fake_lcd_indicator(watch_indicator_t indicator) {
    if (indicator == WATCH_INDICATOR_COLON) return fake_lcd_colon();
    if (lcd_type != WATCH_LCD_TYPE_CUSTOM && indicator > WATCH_INDICATOR_LAP) return false;
    uint8_t value = IndicatorSegments[indicator];
    return screen[SLCD_COMNUM(value)][SLCD_SEGNUM(value)];
}

bool fake_lcd_colon(void) {
    // The addresses watch_set_colon uses.
    return lcd_type == WATCH_LCD_TYPE_CUSTOM ? screen[0][0] : screen[1][16];
}

bool fake_lcd_decimal(void) {
    return lcd_type == WATCH_LCD_TYPE_CUSTOM && screen[0][14];
}

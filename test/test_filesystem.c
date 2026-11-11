#define _POSIX_C_SOURCE 200809L  // alarm
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "filesystem.h"
#include "lfs.h"

// RAM stand-in for the watch's RWW EEPROM. While fail_progs is set, every
// program fails as the hardware reports it: watch_storage_write returns false.
#define ROWS (NVMCTRL_RWWEE_PAGES / 4)
static uint8_t flash[ROWS][NVMCTRL_ROW_SIZE];
static bool fail_progs;

bool watch_storage_read(uint32_t row, uint32_t offset, uint8_t *buffer, uint32_t size) {
    memcpy(buffer, &flash[row][offset], size);
    return true;
}

bool watch_storage_write(uint32_t row, uint32_t offset, const uint8_t *buffer, uint32_t size) {
    if (fail_progs) return false;
    memcpy(&flash[row][offset], buffer, size);
    return true;
}

bool watch_storage_erase(uint32_t row) {
    memset(flash[row], 0xff, NVMCTRL_ROW_SIZE);
    return true;
}

bool watch_storage_sync(void) {
    return true;
}

void delay_ms(const uint16_t ms) {
    (void)ms;
}

extern lfs_t eeprom_filesystem;

// Larger than a littlefs inline file, so a write programs flash blocks.
#define BIG 300

static void fresh_filesystem(void) {
    memset(flash, 0xff, sizeof(flash));
    fail_progs = false;
    // Not CHECKed: after formatting a blank flash, filesystem_init() returns
    // false even though the mount worked (its only caller ignores the result).
    // The writes and reads below fail if the filesystem isn't usable.
    filesystem_init();
}

static void fill(char *buf, char c) {
    memset(buf, c, BIG);
}

static bool file_is(char *name, const char *expected, int32_t length) {
    char buf[BIG * 2 + 1];
    if (filesystem_get_file_size(name) != length) return false;
    if (!filesystem_read_file(name, buf, sizeof(buf))) return false;
    return memcmp(buf, expected, length) == 0;
}

// A failed write must report failure, keep the old contents, and release the
// shared file handle so the next open works.
static void test_failed_write_keeps_old_contents(void) {
    char old[BIG], attempt[BIG], later[BIG];
    fill(old, 'o');
    fill(attempt, 'a');
    fill(later, 'l');

    fresh_filesystem();
    CHECK(filesystem_write_file("pet.bin", old, BIG));

    fail_progs = true;
    CHECK(!filesystem_write_file("pet.bin", attempt, BIG));
    fail_progs = false;

    CHECK(file_is("pet.bin", old, BIG));
    CHECK(filesystem_write_file("pet.bin", later, BIG));
    CHECK(file_is("pet.bin", later, BIG));
}

static void test_failed_append_keeps_old_contents(void) {
    char old[BIG], attempt[BIG], expected[BIG + 1];
    fill(old, 'o');
    fill(attempt, 'a');

    fresh_filesystem();
    CHECK(filesystem_write_file("log.txt", old, BIG));

    fail_progs = true;
    CHECK(!filesystem_append_file("log.txt", attempt, BIG));
    fail_progs = false;

    CHECK(file_is("log.txt", old, BIG));
    CHECK(filesystem_append_file("log.txt", "!", 1));
    memcpy(expected, old, BIG);
    expected[BIG] = '!';
    CHECK(file_is("log.txt", expected, BIG + 1));
}

int main(void) {
    // A leaked handle makes littlefs's list of open files loop on itself, so
    // the bug shows up as a hang: fail instead of waiting forever.
    alarm(10);
    test_failed_write_keeps_old_contents();
    test_failed_append_keeps_old_contents();
    return check_summary("test_filesystem");
}

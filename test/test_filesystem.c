#define _POSIX_C_SOURCE 200809L  // alarm
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "filesystem.h"
#include "lfs.h"

extern const struct lfs_config watch_lfs_cfg;

// RAM stand-in for the watch's RWW EEPROM. While fail_progs is set, every
// program fails as the hardware reports it: watch_storage_write returns false.
// While fail_data_reads is set, reads fail outside rows 0 and 1, which hold the
// superblock and root directory of a freshly formatted filesystem, so a file
// can still be found and opened but not read.
#define ROWS (NVMCTRL_RWWEE_PAGES / 4)
static uint8_t flash[ROWS][NVMCTRL_ROW_SIZE];
static bool fail_progs;
static bool fail_data_reads;

bool watch_storage_read(uint32_t row, uint32_t offset, uint8_t *buffer, uint32_t size) {
    if (fail_data_reads && row > 1) return false;
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

static bool mounted;

// Each test mounts its own filesystem; unmount frees littlefs's buffers.
static void unmount(void) {
    if (mounted) lfs_unmount(&eeprom_filesystem);
    mounted = false;
}

static void fresh_filesystem(void) {
    unmount();
    memset(flash, 0xff, sizeof(flash));
    fail_progs = false;
    fail_data_reads = false;
    // Not CHECKed: after formatting a blank flash, filesystem_init() returns
    // false even though the mount worked (its only caller ignores the result).
    // The writes and reads below fail if the filesystem isn't usable.
    filesystem_init();
    mounted = true;
}

// Unmount and mount again, so the next read comes from flash, not a cache.
static void remount(void) {
    unmount();
    CHECK(lfs_mount(&eeprom_filesystem, &watch_lfs_cfg) == LFS_ERR_OK);
    mounted = true;
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

// A small file lives inline in the directory, so its write only fills a cache
// and the failure comes when close commits it.
static void test_failed_commit_keeps_old_contents(void) {
    fresh_filesystem();
    CHECK(filesystem_write_file("location.u32", "abcd", 4));

    fail_progs = true;
    CHECK(!filesystem_write_file("location.u32", "wxyz", 4));
    fail_progs = false;

    CHECK(file_is("location.u32", "abcd", 4));
    CHECK(filesystem_write_file("location.u32", "1234", 4));
    CHECK(file_is("location.u32", "1234", 4));
}

static void test_negative_length_is_refused(void) {
    char old[BIG];
    fill(old, 'o');

    fresh_filesystem();
    CHECK(filesystem_write_file("pet.bin", old, BIG));
    CHECK(!filesystem_write_file("pet.bin", old, -1));
    CHECK(file_is("pet.bin", old, BIG));
}

static void test_failed_read_releases_the_file(void) {
    char data[BIG], buf[BIG];
    fill(data, 'd');

    fresh_filesystem();
    CHECK(filesystem_write_file("pet.bin", data, BIG));
    remount();

    fail_data_reads = true;
    CHECK(!filesystem_read_file("pet.bin", buf, BIG));
    fail_data_reads = false;

    CHECK(file_is("pet.bin", data, BIG));
}

static void test_read_line(void) {
    char buf[8];
    int32_t offset = 0;

    fresh_filesystem();
    CHECK(filesystem_write_file("lines.txt", "one\ntwo\n", 8));

    CHECK(filesystem_read_line("lines.txt", buf, &offset, 7));
    CHECK(strcmp(buf, "one") == 0);
    CHECK_EQ_INT(offset, 4, "after the first line");
    CHECK(filesystem_read_line("lines.txt", buf, &offset, 7));
    CHECK(strcmp(buf, "two") == 0);
    CHECK_EQ_INT(offset, 8, "after the second line");
}

static void test_failed_read_line_releases_the_file(void) {
    char data[BIG], buf[8];
    int32_t offset = 0;
    fill(data, 'd');
    data[2] = '\n';

    fresh_filesystem();
    CHECK(filesystem_write_file("lines.txt", data, BIG));
    remount();

    fail_data_reads = true;
    CHECK(!filesystem_read_line("lines.txt", buf, &offset, 7));
    fail_data_reads = false;
    CHECK_EQ_INT(offset, 0, "a failed read leaves the offset alone");

    CHECK(filesystem_read_line("lines.txt", buf, &offset, 7));
    CHECK(strcmp(buf, "dd") == 0);
    CHECK_EQ_INT(offset, 3, "after the first line");
}

int main(void) {
    // A leaked handle trips littlefs's "file already open" assert on the next
    // open. The alarm is a backstop in case a bug hangs instead.
    alarm(10);
    test_failed_write_keeps_old_contents();
    test_failed_append_keeps_old_contents();
    test_failed_commit_keeps_old_contents();
    test_negative_length_is_refused();
    test_failed_read_releases_the_file();
    test_read_line();
    test_failed_read_line_releases_the_file();
    unmount();
    return check_summary("test_filesystem");
}

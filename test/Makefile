# Host-side unit tests: plain C, built with the PC's compiler, with no ARM
# toolchain and no hardware headers. Run from the repo root: make -C test

# CC is make's default (cc) unless the environment sets it (the nix shell sets gcc).
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CPPFLAGS := -I. -I../lib/zodiac -I../lib/planetary -I../lib/sunriset -I../lib/sun_events -I../lib/pet -I../lib/zone_offset -I../utz
LDLIBS := -lm

# One program per test file: add test_<name> to TESTS and list its sources
# as test_<name>_SRCS.
TESTS := test_zodiac test_planetary test_sun_events test_pet test_filesystem test_zone_offset test_display
TESTS += test_face_timer test_face_countdown
test_zodiac_SRCS := test_zodiac.c ../lib/zodiac/zodiac.c ../lib/sunriset/sunriset.c
test_planetary_SRCS := test_planetary.c ../lib/planetary/planetary.c ../lib/sun_events/sun_events.c ../lib/sunriset/sunriset.c
test_sun_events_SRCS := test_sun_events.c ../lib/sun_events/sun_events.c ../lib/sunriset/sunriset.c
test_pet_SRCS := test_pet.c ../lib/pet/pet.c
# The real utz zone table and rule lookup.
test_zone_offset_SRCS := test_zone_offset.c ../lib/zone_offset/zone_offset.c ../utz/utz.c ../utz/zones.c
# filesystem.c with real littlefs, over a RAM flash; stubs/ stands in for the
# watch and movement headers it includes.
test_filesystem_SRCS := test_filesystem.c ../filesystem/filesystem.c ../littlefs/lfs.c ../littlefs/lfs_util.c ../lib/base64/base64.c
build/test_filesystem: CPPFLAGS += -Istubs -I../filesystem -I../littlefs -I../lib/base64
# The shared display code over the fake LCD in fakes/. watch_slcd.h includes the
# real watch.h, which needs a board pins.h (stubs/board) and gossamer's eic.h.
test_display_SRCS := test_display.c fakes/fake_lcd.c ../watch-library/shared/watch/watch_common_display.c
build/test_display: CPPFLAGS += -Ifakes -Istubs/board -I../watch-library/shared/watch -I../gossamer/common

# Face tests: one watch face on the harness in harness/, which stands in for
# movement and the hardware (see harness/harness.h). A new face test is
# test_face_<name>.c, test_face_<name> in TESTS, and its _SRCS line naming the
# face's .c. Tests include the face as "<type>/<name>_face.h".
# harness/ comes first so its pins.h is the one the real watch.h includes;
# stubs/ stays out, because its watch.h would shadow the real one.
HARNESS_SRCS := harness/harness.c harness/harness_hw.c fakes/fake_lcd.c \
    ../watch-library/shared/watch/watch_common_display.c ../watch-library/shared/watch/watch_utility.c \
    ../lib/zone_offset/zone_offset.c ../utz/utz.c ../utz/zones.c
build/test_face_%: CPPFLAGS += -Iharness -Ifakes -I.. -I../watch-faces -I../watch-library/shared/watch \
    -I../watch-library/shared/driver -I../gossamer/common
test_face_timer_SRCS := test_face_timer.c ../watch-faces/complication/timer_face.c $(HARNESS_SRCS)
test_face_countdown_SRCS := test_face_countdown.c ../watch-faces/complication/countdown_face.c $(HARNESS_SRCS)

.PHONY: test clean
test: $(addprefix build/,$(TESTS))
	@set -e; for t in $^; do ./$$t; done

.SECONDEXPANSION:
# Any header in test/ or lib/ triggers a rebuild, so a header-only change is never
# tested against a stale binary.
HEADERS := $(wildcard *.h stubs/*.h stubs/board/*.h fakes/*.h harness/*.h ../movement.h ../watch-faces/*/*.h ../lib/*/*.h ../filesystem/*.h ../littlefs/*.h ../utz/*.h \
    ../watch-library/shared/watch/*.h)

build/%: $$($$*_SRCS) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $($*_SRCS) $(LDLIBS)

clean:
	rm -rf build

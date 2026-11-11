# Host-side unit tests: plain C, built with the PC's compiler, with no ARM
# toolchain and no hardware headers. Run from the repo root: make -C test

# CC is make's default (cc) unless the environment sets it (the nix shell sets gcc).
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CPPFLAGS := -I. -I../lib/zodiac -I../lib/planetary -I../lib/sunriset -I../lib/pet
LDLIBS := -lm

# One program per test file: add test_<name> to TESTS and list its sources
# as test_<name>_SRCS.
TESTS := test_zodiac test_planetary test_pet
test_zodiac_SRCS := test_zodiac.c ../lib/zodiac/zodiac.c
test_planetary_SRCS := test_planetary.c ../lib/planetary/planetary.c ../lib/sunriset/sunriset.c
test_pet_SRCS := test_pet.c ../lib/pet/pet.c

.PHONY: test clean
test: $(addprefix build/,$(TESTS))
	@set -e; for t in $^; do ./$$t; done

.SECONDEXPANSION:
# Any header in test/ or lib/ triggers a rebuild, so a header-only change is never
# tested against a stale binary.
HEADERS := $(wildcard *.h ../lib/*/*.h)

build/%: $$($$*_SRCS) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $($*_SRCS) $(LDLIBS)

clean:
	rm -rf build

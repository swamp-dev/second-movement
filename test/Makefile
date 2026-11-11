# Host-side unit tests: plain C, built with the PC's compiler, with no ARM
# toolchain and no hardware headers. Run from the repo root: make -C test

CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CPPFLAGS := -I. -I../lib/zodiac

# One program per test file. List each test's sources as <name>_SRCS.
TESTS := test_zodiac
test_zodiac_SRCS := test_zodiac.c ../lib/zodiac/zodiac.c

.PHONY: test clean
test: $(addprefix build/,$(TESTS))
	@set -e; for t in $^; do ./$$t; done

.SECONDEXPANSION:
build/%: $$($$*_SRCS) check.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $($*_SRCS)

clean:
	rm -rf build

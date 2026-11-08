# ==============================================================================
# The Apparatus - Master Makefile
# Delegates all operations to ./test_and_build.sh for 100% portability.
# ==============================================================================

.PHONY: all test test-unit test-pi test-gui build build-prod build-sim sim clean help

all:
	@./test_and_build.sh all

test:
	@./test_and_build.sh test

test-unit:
	@./test_and_build.sh test-unit

test-pi:
	@./test_and_build.sh test-pi

test-gui:
	@./test_and_build.sh test-gui

build:
	@./test_and_build.sh build

build-prod:
	@./test_and_build.sh build-prod

build-sim:
	@./test_and_build.sh build-sim

sim:
	@./test_and_build.sh sim

clean:
	@./test_and_build.sh clean

help:
	@./test_and_build.sh help

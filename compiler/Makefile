# The Arche compiler, written in Arche-0 and built by stage0.
#
#   make             build build/arche
#   make check       every conformance test, built with the Arche compiler and
#                    LLVM; every valid program, the compiler's own source
#                    included, is accepted; CST round trips; golden
#                    diagnostics (tests/)
#   make differential  stage0 and the Arche compiler agree on every program
#   make check-sanitized  the compiler, built with gcc's AddressSanitizer and
#                    UBSan, builds itself and every test, debug and release
#   make clean       remove build/

STAGE0 := ../stage0
BUILD := build
SOURCES := $(wildcard *.arche */*.arche */*/*.arche)
# Windows starts no script of its own: the runner is given a shell and the
# script to run in it (docs/sprints.md S19).
ifeq ($(OS),Windows_NT)
BUILD_CMD := sh ./arche-build
TESTS_CMD := sh ./arche-build-tests
else
BUILD_CMD := ./arche-build
TESTS_CMD := ./arche-build-tests
endif

all: $(BUILD)/arche

$(BUILD)/arche: $(SOURCES) $(STAGE0)/runtime/arche0_rt.c $(STAGE0)/runtime/arche0_rt.h | $(BUILD)
	$(MAKE) -C $(STAGE0)
	$(STAGE0)/arche0-build main.arche $@

$(BUILD):
	mkdir -p $@

# The compiler's own backend and linker: all the tests again, built with no
# LLVM and no lld, whose absence is enforced once clang has built the runtime,
# and what they do not look at. Needs lld for the loader, and gdb for the session.
check-own: $(BUILD)/arche
	$(MAKE) -C ../conformance
	ARCHE_BACKEND=own sh ./arche-build tests/own/sample.arche $(BUILD)/own-sample
	ARCHE_BACKEND=own CLANG=false LLD=false ../conformance/runner/build/runner --timeout 30 --compiler "$(BUILD_CMD)" ../conformance/tests
	ARCHE_BACKEND=own CLANG=false LLD=false ../conformance/runner/build/runner --timeout 30 --compiler "$(TESTS_CMD)" ../conformance/testrunner
	ARCHE_BACKEND=own sh ./differential.sh ../conformance/tests
	sh ./own-check.sh
	sh ./abi-check.sh

check: $(BUILD)/arche
	$(MAKE) -C ../conformance
	../conformance/runner/build/runner --timeout 30 --compiler "$(BUILD_CMD)" ../conformance/tests
	../conformance/runner/build/runner --timeout 30 --compiler "$(TESTS_CMD)" ../conformance/testrunner
	sh ./check-valid.sh $(BUILD)/arche ../conformance/tests
	sh ./roundtrip.sh $(BUILD)/arche ../conformance/tests .
	sh ./tests/run.sh $(BUILD)/arche
	sh ./explain-check.sh $(BUILD)/arche
	sh ./doctest.sh $(BUILD)/arche
	python3 -I ../editors/check-grammar.py ..

clean:
	rm -rf $(BUILD)

differential: $(BUILD)/arche
	sh ./differential.sh ../conformance/tests

# Undefined behaviour in the compiler shows on some C compilers and not on
# others, as a use after free in the optimizer did, which made a release
# bootstrap differ on gcc 13 only. Built with the sanitizers, it shows every
# time: the compiler builds itself and every test, debug and release.
SAN_CC ?= gcc
SAN_FLAGS := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
check-sanitized: | $(BUILD)
	$(MAKE) -C $(STAGE0)
	$(MAKE) -C ../conformance
	CC=$(SAN_CC) A0_CFLAGS="$(SAN_FLAGS)" $(STAGE0)/arche0-build main.arche $(BUILD)/arche-sanitized
	for mode in "" --release --lib-std "--lib-std --release"; do \
		ASAN_OPTIONS=detect_leaks=0 $(BUILD)/arche-sanitized --lib ../lib/ $$mode build main.arche $(BUILD)/sanitized.ll || exit 1; \
	done
	ARCHE=$(CURDIR)/$(BUILD)/arche-sanitized ARCHE_FLAGS="--lib $(CURDIR)/../lib/" ASAN_OPTIONS=detect_leaks=0 \
		../conformance/runner/build/runner --timeout 120 --compiler "$(BUILD_CMD)" ../conformance/tests
	ARCHE=$(CURDIR)/$(BUILD)/arche-sanitized ARCHE_FLAGS="--lib $(CURDIR)/../lib/ --release" ASAN_OPTIONS=detect_leaks=0 \
		../conformance/runner/build/runner --timeout 120 --compiler "$(BUILD_CMD)" ../conformance/tests

.PHONY: all check differential clean check-own check-sanitized

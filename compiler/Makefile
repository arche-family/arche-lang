# The Arche compiler, written in Arche-0 and built by stage0.
#
#   make             build build/arche
#   make check       every conformance test, built with the Arche compiler and
#                    LLVM; every valid program, the compiler's own source
#                    included, is accepted; CST round trips; golden
#                    diagnostics (tests/)
#   make differential  stage0 and the Arche compiler agree on every program
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

.PHONY: all check differential clean

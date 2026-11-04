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
SOURCES := $(shell find . -name '*.arche' -not -path './build/*')

all: $(BUILD)/arche

$(BUILD)/arche: $(SOURCES) | $(BUILD)
	$(MAKE) -C $(STAGE0)
	$(STAGE0)/arche0-build main.arche $@

$(BUILD):
	mkdir -p $@

check: $(BUILD)/arche
	$(MAKE) -C ../conformance
	../conformance/runner/build/runner --timeout 30 --compiler ./arche-build ../conformance/tests
	../conformance/runner/build/runner --timeout 30 --compiler ./arche-build-tests ../conformance/testrunner
	sh ./check-valid.sh $(BUILD)/arche ../conformance/tests
	sh ./roundtrip.sh $(BUILD)/arche ../conformance/tests .
	sh ./tests/run.sh $(BUILD)/arche

clean:
	rm -rf $(BUILD)

differential: $(BUILD)/arche
	sh ./differential.sh ../conformance/tests

.PHONY: all check differential clean

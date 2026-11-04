# The Arche compiler, written in Arche-0 and built by stage0.
#
#   make             build build/arche
#   make check       every conformance test that names an error code; every
#                    valid program, the compiler's own source included, is
#                    accepted; CST round trips; golden diagnostics (tests/)
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
	../conformance/runner/build/runner --codes E01,E02,E03,E04,E05,E06,E07 --compiler ./arche-check ../conformance/tests
	sh ./check-valid.sh $(BUILD)/arche ../conformance/tests
	sh ./roundtrip.sh $(BUILD)/arche ../conformance/tests .
	sh ./tests/run.sh $(BUILD)/arche

clean:
	rm -rf $(BUILD)

.PHONY: all check clean

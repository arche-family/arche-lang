# The build driver (arche build, arche run), in full Arche, built by the
# Arche compiler.
#
#   make          build/arche-driver
#   make check    clean and cached builds are bit-identical (check.sh)

BUILD := build
LIB := $(wildcard ../lib/*.arche ../lib/*/*.arche ../lib/*/*/*.arche)
RUNTIME := ../compiler/runtime/rt.c ../compiler/runtime/math.inc

all: $(BUILD)/arche-driver

$(BUILD)/arche-driver: main.arche $(LIB) $(RUNTIME)
	$(MAKE) -C ../compiler
	mkdir -p $(BUILD)
	../compiler/arche-build main.arche $@

check: $(BUILD)/arche-driver
	sh ./check.sh $(BUILD)/arche-driver

clean:
	rm -rf $(BUILD) example/build

.PHONY: all check clean

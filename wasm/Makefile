# The WASM runtime (docs/sprints.md S31), in full Arche, built by the Arche
# compiler.
#
#   make          build/arche-wasm
#   make check    the WebAssembly spec's tests (spec.sh)

BUILD := build
LIB := $(wildcard ../lib/*.arche ../lib/*/*.arche)
RUNTIME := $(wildcard ../compiler/runtime/rt.c ../compiler/runtime/*.inc)

all: $(BUILD)/arche-wasm

$(BUILD)/arche-wasm: $(wildcard *.arche) $(LIB) $(RUNTIME)
	$(MAKE) -C ../compiler
	mkdir -p $(BUILD)
	../compiler/arche-build main.arche $@

check: $(BUILD)/arche-wasm
	sh ./spec.sh $(BUILD)/arche-wasm

clean:
	rm -rf $(BUILD)

.PHONY: all check clean

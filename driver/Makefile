# The build driver (arche build, arche run), in full Arche, built by the
# Arche compiler.
#
#   make          build/arche-driver
#   make check    clean and cached builds are bit-identical (check.sh);
#                 packages are checked against the registry (pkg-check.sh);
#                 a real session with the language server (lsp-check.py)

BUILD := build
LIB := $(wildcard ../lib/*.arche ../lib/*/*.arche ../lib/*/*/*.arche)
RUNTIME := $(wildcard ../compiler/runtime/rt.c ../compiler/runtime/*.inc)

all: $(BUILD)/arche-driver $(BUILD)/arche-registry $(BUILD)/arche-lsp

$(BUILD)/arche-driver: main.arche pkg.arche $(LIB) $(RUNTIME)
	$(MAKE) -C ../compiler
	mkdir -p $(BUILD)
	../compiler/arche-build main.arche $@

$(BUILD)/arche-registry: registry.arche $(LIB) $(RUNTIME)
	$(MAKE) -C ../compiler
	mkdir -p $(BUILD)
	../compiler/arche-build registry.arche $@

$(BUILD)/arche-lsp: lsp.arche lsp_proto.arche $(LIB) $(RUNTIME)
	$(MAKE) -C ../compiler
	mkdir -p $(BUILD)
	../compiler/arche-build lsp.arche $@

check: $(BUILD)/arche-driver $(BUILD)/arche-registry $(BUILD)/arche-lsp
	sh ../compiler/arche-test pkg.arche
	sh ./check.sh $(BUILD)/arche-driver
	sh ./pkg-check.sh $(BUILD)/arche-driver $(BUILD)/arche-registry
	python3 -I lsp-check.py $(BUILD)/arche-lsp ..

clean:
	rm -rf $(BUILD) example/build

.PHONY: all check clean

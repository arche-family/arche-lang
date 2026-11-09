# The KV store's crash test (docs/sprints.md S33).
#
#   make          build/kvcrash
#   make check    crash.sh: 30 kills of a writer, and what is left checked

BUILD := build
LIB := $(wildcard ../../lib/*.arche ../../lib/*/*.arche)
RUNTIME := $(wildcard ../../compiler/runtime/rt.c ../../compiler/runtime/*.inc)

all: $(BUILD)/kvcrash

$(BUILD)/kvcrash: main.arche $(LIB) $(RUNTIME)
	$(MAKE) -C ../../compiler
	mkdir -p $(BUILD)
	../../compiler/arche-build main.arche $@

check: $(BUILD)/kvcrash
	bash ./crash.sh $(BUILD)/kvcrash 30

clean:
	rm -rf $(BUILD)

.PHONY: all check clean

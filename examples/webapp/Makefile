# The web application of docs/sprints.md S32.
#
#   make          build/webapp
#   make check    check.sh: HTTPS, HTTP/2, files, gzip, the API, workers,
#                 and memory that does not grow with requests

BUILD := build
LIB := $(wildcard ../../lib/*.arche ../../lib/*/*.arche)
RUNTIME := $(wildcard ../../compiler/runtime/rt.c ../../compiler/runtime/*.inc)

all: $(BUILD)/webapp

$(BUILD)/webapp: main.arche $(LIB) $(RUNTIME)
	$(MAKE) -C ../../compiler
	mkdir -p $(BUILD)
	../../compiler/arche-build main.arche $@

check: $(BUILD)/webapp
	bash ./check.sh $(BUILD)/webapp

clean:
	rm -rf $(BUILD)

.PHONY: all check clean

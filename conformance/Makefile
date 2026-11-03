# Conformance tests and their runner.
#
#   make             build the runner
#   make selftest    check the runner against its own sample tests
#   make check       run tests/ with COMPILER (default: stage0)
#   make clean       remove the runner build

CFLAGS ?= -O2 -g
WARN := -std=c11 -Wall -Wextra -Werror
BUILD := runner/build
RUNNER := $(BUILD)/runner

COMPILER ?= ../stage0/arche0-build
RUNNER_FLAGS ?= --stage0

all: $(RUNNER)

$(RUNNER): runner/runner.c | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) $(LDFLAGS) -o $@ runner/runner.c

$(BUILD):
	mkdir -p $@

selftest: $(RUNNER)
	sh runner/selftest/run.sh $(RUNNER)

check: $(RUNNER)
	$(MAKE) -C ../stage0
	@if [ -d tests ]; then \
		CC="$(CC)" $(RUNNER) $(RUNNER_FLAGS) --compiler $(COMPILER) tests; \
	else \
		echo "no tests yet"; \
	fi

clean:
	rm -rf $(BUILD)

.PHONY: all selftest check clean

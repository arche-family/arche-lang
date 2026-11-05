# Conformance tests and their runner, which is written in Arche
# (runner/runner.arche) and built by the Arche compiler.
#
#   make             build the runner (and the compiler, if it is not built)
#   make selftest    check the runner against its own sample tests
#   make check       run tests/ with COMPILER (default: stage0)
#   make clean       remove the runner build

BUILD := runner/build
RUNNER := $(BUILD)/runner

# Windows starts no script of its own: the command is a shell and the
# script to run in it (docs/sprints.md S19).
ifeq ($(OS),Windows_NT)
COMPILER ?= sh ../stage0/arche0-build
else
COMPILER ?= ../stage0/arche0-build
endif
RUNNER_FLAGS ?= --stage0

all: $(RUNNER)

LIB := $(wildcard ../lib/*.arche ../lib/*/*.arche ../lib/*/*/*.arche)

$(RUNNER): runner/runner.arche $(LIB) ../compiler/runtime/rt.c $(wildcard ../compiler/runtime/*.inc) | $(BUILD)
	$(MAKE) -C ../compiler
	../compiler/arche-build runner/runner.arche $@

$(BUILD):
	mkdir -p $@

selftest: $(RUNNER)
	sh runner/selftest/run.sh $(RUNNER)

check: $(RUNNER)
	$(MAKE) -C ../stage0
	@if [ -d tests ]; then \
		CC="$(CC)" $(RUNNER) $(RUNNER_FLAGS) --compiler "$(COMPILER)" tests; \
	else \
		echo "no tests yet"; \
	fi

clean:
	rm -rf $(BUILD)

.PHONY: all selftest check clean

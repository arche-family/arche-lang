# stage0: the Arche-0 → C11 translator.
#
#   make            build build/arche0, and check that the runtime compiles
#   make clean      remove build/

CFLAGS ?= -O2 -g
WARN := -std=c11 -Wall -Wextra -Werror
# On Windows the C library's own headers deprecate fopen and strerror,
# which C11 keeps; the warning would be an error here (docs/sprints.md S19).
ifeq ($(OS),Windows_NT)
WARN += -D_CRT_SECURE_NO_WARNINGS
endif
BUILD := build

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/arche0 $(BUILD)/arche0_rt.o

$(BUILD)/arche0: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) -MMD -MP -c -o $@ $<

# The runtime is compiled again with every program (arche0-build); this
# object only checks that it compiles without warnings.
$(BUILD)/arche0_rt.o: runtime/arche0_rt.c runtime/arche0_rt.h | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) -c -o $@ runtime/arche0_rt.c

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)

.PHONY: all clean

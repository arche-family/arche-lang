# stage0: the Arche-0 → C11 translator.
#
#   make            build build/arche0
#   make clean      remove build/

CFLAGS ?= -O2 -g
WARN := -std=c11 -Wall -Wextra -Werror
BUILD := build

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/arche0

$(BUILD)/arche0: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)

.PHONY: all clean

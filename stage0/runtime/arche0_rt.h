// Runtime of programs built by stage0: spec/arche-0.md §10-§11.
// Written in portable C11 so that any C compiler can build stage0's output.

#ifndef ARCHE0_RT_H
#define ARCHE0_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const uint8_t *ptr;
    uint64_t len;
} a0_str;

#define A0_STR(lit, n) ((a0_str){(const uint8_t *)(lit), (n)})

typedef struct {
    const char *file;
    uint32_t line;
    uint32_t col;
} a0_loc;

#define A0_LOC(file, line, col) ((a0_loc){(file), (line), (col)})

// The result of a function returning E!void: 0, or the error's number.
typedef uint16_t a0_err;

// ---- std --------------------------------------------------------------------

typedef struct {
    FILE *f;
} a0_std_Output;

typedef struct {
    uint64_t len;
    a0_str *items;
} a0_std_Args;

typedef struct {
    char unused;
} a0_std_Fs;

typedef struct {
    char unused;
} a0_alloc_Allocator;

typedef struct {
    a0_std_Fs fs;
    a0_std_Output out;
    a0_std_Output err;
    a0_std_Args args;
    a0_alloc_Allocator alloc;
} a0_std_System;

void a0_std_Output_write(a0_std_Output *self, a0_str text);
void a0_std_Output_write_byte(a0_std_Output *self, uint8_t byte);
void a0_std_Output_write_u64(a0_std_Output *self, uint64_t n);
void a0_std_Output_write_i64(a0_std_Output *self, int64_t n);

uint64_t a0_std_Args_len(a0_std_Args *self);
a0_str a0_std_Args_get(a0_std_Args *self, uint64_t i, a0_loc loc);

// ---- panics and traps -------------------------------------------------------

_Noreturn void a0_panic(a0_str message, a0_loc loc);
_Noreturn void a0_trap(const char *message, a0_loc loc);

// ---- provided by the generated program --------------------------------------

a0_err a0_entry(a0_std_System *sys);
extern const char *const a0_error_names[];

#endif

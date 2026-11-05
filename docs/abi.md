# Arche ABI, version 0

This is the binary interface of code the Arche compiler builds: layouts, calling convention, symbol names and the runtime it calls. Version 0 covers Arche-0 on x86-64 Linux, built through LLVM (sprint S8). It changes only with a major release once it is stable (S29).

## Layouts

Sizes and alignments are in bytes. A composite value is laid out in memory at its alignment; padding is zero, because every value is made from zeroed memory, so values of key types (§5.5) can be hashed and compared byte by byte.

| Type | Size | Align | Layout |
| --- | --- | --- | --- |
| `bool` | 1 | 1 | 0 or 1 |
| `i8`/`u8` … `i64`/`u64`, `isize`, `usize` | 1, 2, 4, 8 | the same | two's complement, little-endian |
| `&T`, `&mut T` | 8 | 8 | address |
| `str`, `[]T`, `[]mut T` | 16 | 8 | `{ptr: address, len: u64}` |
| `&dyn I`, `&mut dyn I` | 16 | 8 | `{data: address, table: address}`; the table is the addresses of the type's methods of `I`, in the order `I` declares them |
| `[N]T` | `N × size(T)` | `align(T)` | elements in order |
| struct | sum of fields, padded | max of fields | fields in declaration order, each at its alignment; the size rounds up to the alignment |
| `packed struct` | sum of fields | 1 | fields in declaration order, no padding (spec/arche.md §10.2) |
| struct with `align(N)` | rounded up to `N` | max(fields, `N`) | as a struct; a field with `align(N)` is placed at a multiple of `N` |
| enum | | max(4, payloads) | `{tag: u32}` then, at the payload alignment, the payload of the variant: its fields like a struct's |
| `?T` | | `align(T)` | `{has: u8}` then the `T` at its alignment |
| error set | 2 | 2 | the error's number: 0 for none, from 1 in the order the program first names them |
| `E!T` | | max(2, `align(T)`) | `{error: u16}` then the `T` at its alignment; `E!void` is the `u16` alone |
| `alloc.Allocator` | 8 | 8 | a handle |
| `alloc.Symbol` | 4 | 4 | an index |
| `alloc.List(T)`, `alloc.String` | 32 | 8 | `{data: address, len: u64, cap: u64, allocator}` |
| `alloc.Map(K, V)` | 56 | 8 | runtime-defined |
| `alloc.Interner` | 48 | 8 | runtime-defined |
| `std.Output` | 8 | 8 | runtime-defined |
| `std.Args`, `std.Fs` | 16, 8 | 8 | runtime-defined |
| `std.System` | | 8 | `{fs, stdout, stderr, args, alloc}` in that order; `lib/std.arche`'s adds `{clock, env, proc, net, rand}` after them, where `env` is the address of the environment's array of pointers (`envp`) and the others are empty |

## Calling convention

- A parameter of a scalar type (`bool`, integers, `&T`, error sets, `Allocator`, `Symbol`) is passed as that scalar. A parameter of any other type is passed as the address of the value. The callee does not change it, because parameters are immutable (§6.1).
- A scalar result is returned as that scalar. Any other result is written to memory the caller provides, whose address is passed before the parameters.
- `void` and `noreturn` functions return nothing.

## Symbols

A function's symbol is `a0` followed by each segment of its module path and then its name, each written as `_<length><text>`: `syntax.lexer.lex` is `a0_6syntax_5lexer_3lex`. A method adds its type's name before its own: `a0_4main_3Ast_4eval`. A method of an `impl` (spec/arche.md §5.1) adds `_X<n>` after its name, `n` the number of the impl's syntax node in its module, since two interfaces of a type may have methods of one name. A closure is `closure` with `_C<n>`, `n` its expression's node. An instance of a generic function or type, a method of one, or a closure in one adds `_I<h>`, `h` sixteen hex digits of the FNV-1a hash of its comptime arguments written as types that name their module. So a symbol depends only on its own module's text and its arguments, not on the rest of the program, and a module compiled on its own (`arche units`, below) keeps its symbols when other modules change. The test block numbered `n` among the root module's is `a0_test_<n>`. The method table of impl `n` is `a0vt_<n>`, of closure `n` `a0vc_<n>`; they are private to the module that uses them.

A call through a `&dyn I` loads the method's address from the table and passes the `data` address as `self`; the other parameters and the result follow the rules above. The root module whose file name is not a module path has the empty name, written `_0`. Runtime symbols start with `a0rt_`.

## Units

`arche units <file> <dir>` writes the program as one LLVM module for each of its modules, `<dir>/<n>.ll` in load order, the root module's first, for the build driver (`driver/`), which compiles each to an object kept under the SHA-256 of its text. A unit defines its module's functions, and declares those of other modules it calls; a function another unit calls, or reaches through a method table, is not `internal`. Each unit has its own copies of the strings and method tables it uses, numbered by first use, and names its module in `source_filename`, so its text, and the object made from it, depend on its module alone. The root unit also defines `a0_error_names` and, in a test build, `a0_tests`. Objects are compiled with `-ffunction-sections -fdata-sections` and linked with `--gc-sections`, which drops the functions nothing reaches.

## The runtime

The runtime (`compiler/runtime/rt.c`) has its own entry point and calls no C library. The float functions LLVM calls when the processor has no instruction for an operation, `floor`, `ceil`, `trunc`, `fmod`, `fma` and their `f` forms, are its own too (`compiler/runtime/math.inc`, shared with the freestanding runtime). It talks to Linux through system calls only. It owns `_start`, builds `std.System` from the arguments, calls `a0_entry` (the program's `main`), and exits:

- with 0 when `main` returns;
- with 1 after writing `error: <name>` when `main` returns an error, using the table `a0_error_names` that the program defines;
- with 101 after writing `panic: <message>` and `  at <file>:<line>:<column>` on a panic or a trap.

Outputs are flushed on every exit.

# Arche ABI, version 0

This is the binary interface of code the Arche compiler builds: layouts, calling convention, symbol names and the runtime it calls. Version 0 covers Arche-0 on x86-64, built through LLVM (sprint S8), for Linux and for Windows (S19). It changes only with a major release once it is stable (S29).

Layouts, the calling convention and symbol names are the same on both systems; what differs is the runtime underneath and the file the library's `std` comes from, which are named where it matters below.

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
| `std.System` | | 8 | `{fs, stdout, stderr, args, alloc}` in that order; the library's `std` adds `{clock, env, proc, net, rand, stdin}` after them, where `env` is the address of the environment's array of pointers (`envp`) and the others are empty. Windows keeps no such array: `env` is 0 there, and its `std` asks the system for one name at a time |

## Calling convention

- A parameter of a scalar type (`bool`, integers, `&T`, error sets, `Allocator`, `Symbol`) is passed as that scalar. A parameter of any other type is passed as the address of the value. The callee does not change it, because parameters are immutable (§6.1).
- A scalar result is returned as that scalar. Any other result is written to memory the caller provides, whose address is passed before the parameters.
- `void` and `noreturn` functions return nothing.

## Symbols

A function's symbol is `a0` followed by each segment of its module path and then its name, each written as `_<length><text>`: `syntax.lexer.lex` is `a0_6syntax_5lexer_3lex`. A method adds its type's name before its own: `a0_4main_3Ast_4eval`. A method of an `impl` (spec/arche.md §5.1) adds `_X<n>` after its name, `n` the number of the impl's syntax node in its module, since two interfaces of a type may have methods of one name. A closure is `closure` with `_C<n>`, `n` its expression's node. An instance of a generic function or type, a method of one, or a closure in one adds `_I<h>`, `h` sixteen hex digits of the FNV-1a hash of its comptime arguments written as types that name their module. So a symbol depends only on its own module's text and its arguments, not on the rest of the program, and a module compiled on its own (`arche units`, below) keeps its symbols when other modules change. The test block numbered `n` among the root module's is `a0_test_<n>`. The method table of impl `n` is `a0vt_<n>`, of closure `n` `a0vc_<n>`; they are private to the module that uses them.

A call through a `&dyn I` loads the method's address from the table and passes the `data` address as `self`; the other parameters and the result follow the rules above. The root module whose file name is not a module path has the empty name, written `_0`. Runtime symbols start with `a0rt_`.

## Units

`arche units <file> <dir>` writes the program as one LLVM module for each of its modules, `<dir>/<n>.ll` in load order, the root module's first, for the build driver (`driver/`), which compiles each to an object kept under the SHA-256 of its text. A unit defines its module's functions, and declares those of other modules it calls; a function another unit calls, or reaches through a method table, is not `internal`. Each unit has its own copies of the strings and method tables it uses, numbered by first use, and names its module in `source_filename`, so its text, and the object made from it, depend on its module alone. The root unit also defines `a0_error_names` and, in a test build, `a0_tests`. Objects are compiled with `-ffunction-sections -fdata-sections` and linked with `--gc-sections`, which drops the functions nothing reaches.

## Systems

A program is built for one system, which the compiler is told with `--target` (`linux-x86_64`, `windows-x86_64`) and which picks three things:

- The target triple of the LLVM module: `x86_64-pc-linux-gnu` or `x86_64-pc-windows-msvc`.
- The file the library's `std` is read from: `lib/std.arche` for Linux, `lib/std-windows.arche` for Windows. The module is `std` either way, so a program that imports it says nothing about the system; `std.SYSTEM` names the system when a program must tell them apart. Each reaches its system through a module of its own, `std.linux` or `std.win32`.
- The half of the runtime that is compiled in, and how the program is linked (`compiler/target.sh`): `-nostdlib -static` on Linux, and on Windows `-nostdlib` with the entry point `a0rt_entry`, the console subsystem, `kernel32`, `ws2_32` and `advapi32`, and `-Brepro`, which leaves the time of the link out of the file so that the same input gives the same bytes.

## The runtime

The runtime (`compiler/runtime/rt.c`) has its own entry point and calls no C library. The float functions LLVM calls when the processor has no instruction for an operation, `floor`, `ceil`, `trunc`, `fmod`, `fma` and their `f` forms, are its own too (`compiler/runtime/math.inc`, shared with the freestanding runtime).

Everything that one system alone has is behind the `os_` functions declared at the top of `rt.c` and defined in `linux.inc` or `windows.inc`, which the file includes at its end: memory, files and output, the process, the switch between fibers, the poll a task waits in, and a child process for each test. An operation on a file answers with what it did, or with one of the `IO_` codes negated, so that a failure reads the same on both. On Linux the runtime talks to the kernel through system calls; on Windows it calls Win32 alone, keeps paths as UTF-16 at the edge, tells the thread block about a fiber's stack on every switch, and brings its own `__chkstk`, which walks the stack it is on instead of the one the system knows about.

It owns the entry point, `_start` on Linux and `a0rt_entry` on Windows, builds `std.System` from the arguments (on Windows by splitting the command line the way every other program there does, and translating it to UTF-8), calls `a0_entry` (the program's `main`), and exits:

- with 0 when `main` returns;
- with 1 after writing `error: <name>` when `main` returns an error, using the table `a0_error_names` that the program defines;
- with 101 after writing `panic: <message>` and `  at <file>:<line>:<column>` on a panic or a trap.

Outputs are flushed on every exit.

`arche test` runs each test in a child process, so that a panic fails that test alone. On Linux the child comes from `fork` and runs the test in place; Windows has no `fork`, so the child is this program started again with `--a0-test <number> <seed>`, which the runtime answers before it reads any argument of the program's own, and its output goes to two files the parent reads once it has ended.

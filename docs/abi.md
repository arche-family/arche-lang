# Arche ABI, version 0

This is the binary interface of code the Arche compiler builds: layouts, calling convention, symbol names and the runtime it calls. Version 0 covers Arche-0 on x86-64, ARM64 and RISC-V 64 (S28), built through LLVM (sprint S8) or by the compiler's own backend (S27), for Linux, and on x86-64 for Windows too (S19). It changes only with a major release, and an object says which version it was built to ("Object files").

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

The stack is aligned to 16 at each call. Non-scalar results stay in memory, on every target, even a slice's two words: a single rule for all of them, which a later major version may refine.

Those scalars go as the processor's C convention has them, so Arche code and the runtime's C call each other:

| Target | Integers, addresses | Floats | Past those | Result | A narrow integer, as an argument |
| --- | --- | --- | --- | --- | --- |
| `linux-x86_64` (System V) | rdi, rsi, rdx, rcx, r8, r9 | xmm0 to xmm7 | the stack, 8 bytes each, the first lowest | rax, xmm0 | below 4 bytes, zero-extended |
| `windows-x86_64` | rcx, rdx, r8, r9 | xmm0 to xmm3, in the same four positions | the stack, above 32 bytes kept for the callee | rax, xmm0 | below 4 bytes, zero-extended |
| `linux-aarch64` (AAPCS64) | x0 to x7 | d0 to d7, s0 to s7 | the stack, 8 bytes each, the first lowest | x0, d0 | below 4 bytes, zero-extended |
| `linux-riscv64` (LP64D) | a0 to a7 | fa0 to fa7, then a free a-register, its bits as they are | the stack, 8 bytes each, the first lowest | a0, fa0 | 4 bytes, sign-extended whatever the type's sign; below that, zero-extended |

The IR does not know a narrow integer's sign, so an `i8` or `i16` goes zero-extended too. A C function taking `signed char` or `short` that trusts its caller, as clang's code does on x86-64 and RISC-V 64, sees a negative one as a large positive number; until the IR knows, pass such a value to C as `i32` and `int`. Arche code never relies on an argument's upper bits, on any target.

## Symbols

A function's symbol is `a0` followed by each segment of its module path and then its name, each written as `_<length><text>`: `syntax.lexer.lex` is `a0_6syntax_5lexer_3lex`. A method adds its type's name before its own: `a0_4main_3Ast_4eval`. A method of an `impl` (spec/arche.md §5.1) adds `_X<n>` after its name, `n` the number of the impl's syntax node in its module, since two interfaces of a type may have methods of one name. A closure is `closure` with `_C<n>`, `n` its expression's node. An instance of a generic function or type, a method of one, or a closure in one adds `_I<h>`, `h` sixteen hex digits of the FNV-1a hash of its comptime arguments written as types that name their module. So a symbol depends only on its own module's text and its arguments, not on the rest of the program, and a module compiled on its own (`arche units`, below) keeps its symbols when other modules change. The test block numbered `n` among the root module's is `a0_test_<n>`. The method table of impl `n` is `a0vt_<n>`, of closure `n` `a0vc_<n>`; they are private to the module that uses them.

A call through a `&dyn I` loads the method's address from the table and passes the `data` address as `self`; the other parameters and the result follow the rules above. The root module whose file name is not a module path has the empty name, written `_0`. Runtime symbols start with `a0rt_`.

## Units

`arche units <file> <dir>` writes the program as one LLVM module for each of its modules, `<dir>/<n>.ll` in load order, the root module's first, for the build driver (`driver/`), which compiles each to an object kept under the SHA-256 of its text. A unit defines its module's functions, and declares those of other modules it calls; a function another unit calls, or reaches through a method table, is not `internal`. Each unit has its own copies of the strings and method tables it uses, numbered by first use, and names its module in `source_filename`, so its text, and the object made from it, depend on its module alone. The root unit also defines `a0_error_names` and, in a test build, `a0_tests`. Objects are compiled with `-ffunction-sections -fdata-sections` and linked with `--gc-sections`, which drops the functions nothing reaches.

## The own backend

`arche build <file> <out>.ao` writes an object with no LLVM, for a debug build on Linux, and `arche link` makes a program of it and the runtime with no lld (below); `ARCHE_BACKEND=own` makes the build scripts and `arche-driver` do so. Named `<out>.o`, the object is an ELF one, for other linkers. What every processor shares is in `compiler/native/` (the ELF and DWARF writers, how long values live and where) and what is its own in `compiler/x64/`, `compiler/a64/` and `compiler/rv64/`. Layouts, the calling convention and symbols are the ones above.

Frames go by a frame pointer. A value lives in a register a callee keeps, or in the frame; the other registers belong to the instruction being written:

| Target | Frame pointer and return address | Integer values | Float values |
| --- | --- | --- | --- |
| `linux-x86_64` | rbp; on the stack above it | rbx, r12 to r15 | xmm8 to xmm13, in memory across a call |
| `linux-aarch64` | x29; x29 and x30 at x29 | x19 to x28 | d8 to d15 |
| `linux-riscv64` | s0, the caller's sp; ra at s0-8 and the caller's s0 at s0-16 | s1 to s11 | fs0 to fs11 |

A static program's data is reached by its absolute address on RISC-V 64 (`lui` and `addi`), so it stays below 2 GB. A branch that cannot reach (±1 MB, for ARM64's conditional ones and RISC-V's jumps) is an error of the compiler's, not bad code. ARM64's atomics are those of ARMv8.0; RISC-V's on bytes and halves use a reservation of the word around them.

The object has DWARF 4: lines, functions, parameters and local variables, their types, and `.debug_frame`. A variable is known in its whole function, since scopes are not told.

It also defines `a0_source_calls` (`{pc: u32, line: u32, function: u32}`, by the address each call returns to), `a0_source_call_count` and `a0_source_functions` (`{name: str, file: str}`). A panic's stack trace reads them, walking the frame pointers through the runtime too, which is built with them for this: each frame adds a line `  in <function> (<file>:<line>)` after the panic's place. A test does not, so that what a run reports is the same for every backend. Other programs have no tables, and nothing is added.

## Object files

`arche build <file> <out>.ao` writes an object in the Arche format, and `arche units --ao <file> <dir>` one for each of the program's modules, `<dir>/<n>.ao`, under the rules of "Units". `arche import <in.o> <out.ao>` makes one of an ELF object, for what clang builds, such as the runtime and LLVM's units. It keeps what a program loads; unwind tables, notes and debug info stay behind.

The file is little-endian. A header of 40 bytes:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | `ARCHEAO\n` |
| 8 | 4 | the format's version: 1 |
| 12 | 4 | the ABI's version, this document's: 0 |
| 16 | 4 | the processor, by ELF's `e_machine`: 62 x86-64, 183 ARM64, 243 RISC-V |
| 20 | 4 | the processor's flags, by ELF's `e_flags`: on RISC-V 64, 4 for the LP64D convention |
| 24 | 4 | the number of sections |
| 28 | 4 | the number of symbols |
| 32 | 4 | the number of relocations |
| 36 | 4 | the size of the names |

Then the three tables, whose entries take 32 bytes each, the names, and the sections' bytes, each section's at its alignment. A linker refuses an object of another format or ABI version.

- **A section:** name (4), kind (4: 1 code, 2 read-only data, 3 data, 4 bss, 5 debug, 6 other, which a linker leaves out), alignment (4, a power of two), 4 of zero, the place of its bytes in the file (8; 0 for a bss), size (8).
- **A symbol:** name (4), section (4; `0xFFFFFFFF` for one the object does not define), kind (1: 0 none, 1 data, 2 function, 3 a section's own), binding (1: 0 local, 1 global, 2 weak), 6 of zero, value (8, from the section's start), size (8).
- **A relocation:** section (4), symbol (4), kind (4), 4 of zero, offset in the section (8), addend (8, signed).
- **A name:** ASCII, ending in a zero byte, by its place in the names. Place 0 is the empty one.

A relocation's kind has the number the processor's ELF supplement gives it, and means the same. S is the symbol's address, A the addend, P the place, G the address of the symbol's slot in the global offset table, which the linker makes:

| Processor | Kind | Name | Writes |
| --- | --- | --- | --- |
| x86-64 | 1 | `R_X86_64_64` | S + A, 8 bytes |
| | 2, 4 | `PC32`, `PLT32` | S + A - P, 4 bytes |
| | 10, 11 | `32`, `32S` | S + A, 4 bytes, as unsigned, as signed |
| ARM64 | 257, 258 | `ABS64`, `ABS32` | S + A, 8 and 4 bytes |
| | 261 | `PREL32` | S + A - P, 4 bytes |
| | 275, 311 | `ADR_PREL_PG_HI21`, `ADR_GOT_PAGE` | the distance to the page of S + A, of G, in `adrp` |
| | 277, 278, 284, 285, 286, 299 | `ADD_ABS_LO12_NC`, `LDST8`, `16`, `32`, `64`, `128_ABS_LO12_NC` | the low 12 bits of S + A, scaled by the access |
| | 312 | `LD64_GOT_LO12_NC` | the low 12 bits of G, scaled by 8 |
| | 279, 280, 282, 283 | `TSTBR14`, `CONDBR19`, `JUMP26`, `CALL26` | (S + A - P) / 4 in the branch |
| RISC-V 64 | 1, 2 | `R_RISCV_32`, `64` | S + A, 4 and 8 bytes |
| | 16, 17 | `BRANCH`, `JAL` | S + A - P in the branch |
| | 19 | `CALL_PLT` | S + A - P in `auipc` and the `jalr` after it |
| | 26, 27, 28 | `HI20`, `LO12_I`, `LO12_S` | S + A in `lui` and the `addi`, load or store after it |

A value that does not fit is the linker's error, as is any other kind. RISC-V's linker relaxation is not one of them: what clang builds for this linker is built with `-mno-relax`.

## Linking

`arche link <out> <in.ao>...` is a static linker, in the compiler (`compiler/link/`), and needs no other tool. It takes the sections of each kind in the order they are given, and lays them out from address `0x400000`: read-only data, with the file's headers, then code, data and bss, each kind on pages of its own. A page is 4 KB, or 64 KB on ARM64, whose kernels may use those. Debug sections go together by name, and are not loaded: an address in one is the offset of what it names, which is what debug info wants.

A global symbol must have one definition: two are an error, unless one is weak, and it goes. A symbol a relocation uses with no definition is an error too, unless it is weak, when its address is 0. The program starts at `_start`. The same objects in the same order give the same bytes.

What it writes is an ELF executable, for Linux and for debuggers, with its symbols and debug info. With `--ax` it is an Arche executable, for Arche OS's loader.

## Arche executables

An Arche executable, `.ax`, is a header and segments, all little-endian:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | `ARCHEAX\n` |
| 8 | 4 | the format's version: 1 |
| 12 | 4 | the ABI's version: 0 |
| 16 | 4 | the processor, as in an object |
| 20 | 4 | the number of segments |
| 24 | 8 | the address of the entry |
| 32 | 8 | the size of a page |

Then for each segment 40 bytes: what it allows (4: 4 read, 2 write, 1 execute), 4 of zero, its address (8), its place in the file (8), its size in the file (8) and in memory (8). What is past its size in the file is zero. A segment's address and place are multiples of the page, so that a loader can map it. A program's addresses are fixed: there is nothing to relocate. `compiler/tests/ax-load.c` is such a loader for today's systems, which gives the program the stack Linux would, because the runtime of a Linux program expects one.

## Testing the ABI

`compiler/abi-check.sh` is the suite, on each processor (`conformance/abi/`). Every layout above crosses between three modules and back: all the integers, floats, more arguments than registers, structs small and large, arrays, enums with and without payloads, optionals, error unions, slices, `str`, references, values that own memory, generic instances, methods, and calls back through method tables and closures. Each module, with the library's, is built separately by LLVM and by the compiler's own backend, and linked by the compiler's linker in four mixes, the program printing `conformance/abi/expected.txt` in all. An Arche executable of it prints the same.

## Systems

A program is built for one system, which the compiler is told with `--target` (`linux-x86_64`, `linux-aarch64`, `linux-riscv64`, `windows-x86_64`) and which picks three things:

- The target triple of the LLVM module: `x86_64-pc-linux-gnu`, `aarch64-unknown-linux-gnu`, `riscv64-unknown-linux-gnu` or `x86_64-pc-windows-msvc`.
- The file the library's `std` is read from: `lib/std.arche` for Linux, whose system calls are numbered in `lib/std/linux-<processor>.arche`, `lib/std-windows.arche` for Windows. The module is `std` either way, so a program that imports it says nothing about the system; `std.SYSTEM` names the system when a program must tell them apart. Each reaches its system through a module of its own, `std.linux` or `std.win32`.
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

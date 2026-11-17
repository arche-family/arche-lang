# Benchmarks: the baseline

The baseline of docs/sprints.md S26, measured on 2026-11-05 with `bench/run.sh`: how fast the compiler compiles, and how fast its programs run, in a debug build and a release build (`arche --release`, the IR optimizer of `compiler/ir/opt.arche`). Times are the best of three runs, in milliseconds.

- Machine: Intel(R) Core(TM) Ultra 7 265KF, Linux.
- Back end: Ubuntu clang version 21.1.8 (6ubuntu1).
- Programs: `bench/programs.arche`; the compiler: `compiler/main.arche` and its modules.

| What | Debug | Release |
| --- | ---: | ---: |
| compiler: itself to LLVM IR (ms) | 197 | 319 |
| compiler: lines of Arche compiled | 26146 | 26146 |
| compiler: LLVM IR (KB) | 8903 | 7490 |
| fib (ms, clang -O0) | 72 | 44 |
| sieve (ms, clang -O0) | 501 | 487 |
| map (ms, clang -O0) | 635 | 480 |
| sort (ms, clang -O0) | 1584 | 1179 |
| fib (ms, clang -O1) | 29 | 29 |
| sieve (ms, clang -O1) | 205 | 207 |
| map (ms, clang -O1) | 179 | 182 |
| sort (ms, clang -O1) | 526 | 536 |

With clang -O0 a program's speed is the Arche optimizer's work alone, as it will be for the compiler's own back end (sprint S27); with -O1, LLVM's passes run after it and do the same work again. The compiler is itself a release build's input: `bootstrap.sh` checks that a release compiler builds itself bit-identically and builds what the debug compiler does.

## The compiler's own backend

docs/sprints.md S41, measured on 2026-11-17 with `bench/run.sh` on the same machine: the programs built by the compiler's own backend, with no LLVM, in a debug build and in a release build, which takes the optimizer's IR (its phis, on their edges). LLVM's rows are of the same run, for comparison. Times are the best of three runs, in milliseconds.

| What | Debug | Release |
| --- | ---: | ---: |
| fib (ms, own backend) | 43 | 28 |
| sieve (ms, own backend) | 393 | 292 |
| map (ms, own backend) | 477 | 379 |
| sort (ms, own backend) | 1302 | 834 |
| fib (ms, clang -O0) | 73 | 42 |
| sieve (ms, clang -O0) | 478 | 457 |
| map (ms, clang -O0) | 705 | 494 |
| sort (ms, clang -O0) | 1631 | 1224 |
| fib (ms, clang -O1) | 29 | 30 |
| sieve (ms, clang -O1) | 206 | 209 |
| map (ms, clang -O1) | 210 | 205 |
| sort (ms, clang -O1) | 558 | 540 |

A release by the own backend is a third faster than its debug build, and than the optimizer's IR through clang -O0. Its branches on an overflow read the flags the operation left, and an optimized function's blocks are laid out so that the code that runs falls through, traps last. It is still slower than LLVM's passes (-O1): it keeps values in five registers (x86-64's that a call keeps), and reads parameters from the frame. Keeping them in registers too was tried, and was slower with five: a parameter lives long and pushes the loop's values out.

## The web server

The baseline of docs/sprints.md S33, measured on 2026-11-09 with `bench/web/run.sh`: one worker in the clear, 16 keep-alive HTTP/1.1 connections of 2000 requests each, made by `bench/web/load.arche`, which times each request from its first byte sent to its last byte received.

| What | Requests a second | p99 (µs) |
| --- | ---: | ---: |
| `examples/webapp`: a JSON answer (`/api/health`) | 242560 | 180 |
| `examples/webapp`: a file from the disk (`/index.html`) | 74384 | 552 |
| `arche-wasm serve`: a WASM handler in the interpreter (`/hello`) | 205654 | 200 |

On GitHub's runners (`ci`, from two runs of 2026-11-09, the slower of each): 77022 requests a second for JSON at a p99 of 381 µs, 16259 for a file at 4334 µs, 64249 for the WASM handler at 448 µs.

The numbers are kept in `bench/web/baseline.txt`, a line for each kind of machine (`local` here, `ci` for GitHub's runners), and `run.sh` fails when throughput falls under a third of them or p99 grows past four times: a regression, not a busy machine. A file costs most: while tasks run, a file's system calls that would wait for the disk go to a thread of the runtime's and the task waits in the poller (compiler/runtime/linux.inc), and the ones that would not (a read the page cache has, preadv2 with RWF_NOWAIT; an open of a path the kernel knows, openat2 with RESOLVE_CACHED) are made at once. Measured again on 2026-11-09 after that change: the file's numbers are those above.

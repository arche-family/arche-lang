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

## The web server

The baseline of docs/sprints.md S33, measured on 2026-11-09 with `bench/web/run.sh`: one worker in the clear, 16 keep-alive HTTP/1.1 connections of 2000 requests each, made by `bench/web/load.arche`, which times each request from its first byte sent to its last byte received.

| What | Requests a second | p99 (µs) |
| --- | ---: | ---: |
| `examples/webapp`: a JSON answer (`/api/health`) | 248773 | 193 |
| `examples/webapp`: a file from the disk (`/index.html`) | 84720 | 398 |
| `arche-wasm serve`: a WASM handler in the interpreter (`/hello`) | 215558 | 158 |

The numbers are kept in `bench/web/baseline.txt`, a line for each kind of machine (`local` here, `ci` for GitHub's runners), and `run.sh` fails when throughput falls under a third of them or p99 grows past four times: a regression, not a busy machine. A file costs most because reading one stops every task of the worker (files do not use the poller yet).

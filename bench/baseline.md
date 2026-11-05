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

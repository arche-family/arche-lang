# Conformance tests

Tests of the Arche specification. Every rule gets at least one positive and one negative test.

| Path | Contents |
| --- | --- |
| `tests/` | the tests, grouped by spec chapter |
| `runner/` | the test runner, in Arche (`runner.arche`), built by the Arche compiler; it replaced the C11 runner of S0 in S23 |
| `runner/selftest/` | sample tests and a mock compiler that check the runner itself |
| `testrunner/` | tests of `arche test` and `std.testing`, built with `compiler/arche-build-tests` |

```sh
make -C conformance selftest                 # check the runner
make -C conformance check                    # run tests/ with stage0
conformance/runner/build/runner --stage0 --compiler stage0/arche0-build conformance/tests/<chapter>/<name>.arche   # one test
```

The compiler command may carry arguments of its own, so that a system
which starts no script of its own is given a shell and the script:
`--compiler "sh ./arche-build"` (docs/sprints.md S19).

## Test format

A test is one `.arche` file. Its header is the run of `// key: value` lines at the top of the file; the first line of another form ends it. An unknown key is an error, so an ordinary comment in the header must not look like `// word: …`.

```arche
// test: run
// spec: 7.2
// exit: 101
// stderr: panic:
import std;

fn main(sys: &mut std.System) -> !void {
    let zero: u32 = 0;
    sys.stdout.write_u64((1 / zero) as u64);
}
```

```arche
// test: compile-error
// spec: 5.6
// line: 6
// stage0: skip
fn f() {
    let x = 5;
}
```

| Key | Test kind | Meaning |
| --- | --- | --- |
| `test` | both | `run` or `compile-error`; required |
| `spec` | both | the spec section the test covers, e.g. `7.2`; required |
| `stage0` | both | `skip`: the rule is in spec §12, and stage0 may accept the program |
| `wasm` | both | `skip`: the test needs what WebAssembly's system does not have, such as processes, sockets or tasks, and is skipped with `--arch wasm32` |
| `system` | both | `linux` or `windows`: the test is only for that system, and is skipped on the others. For what one system alone has, such as its own system calls; a rule that holds everywhere gets a test without this key |
| `arch` | both | `x86_64`, `aarch64` or `riscv64`: the test is only for that processor, and is skipped on the others (the runner's `--arch`, x86_64 unless given). For a processor's own instructions |
| `exit` | `run` | expected exit status; default 0 |
| `stdout` | `run` | one line of expected stdout; repeated lines build it, each followed by `\n`; no `stdout` lines means no output |
| `stderr` | `run` | text stderr must contain; repeated lines must appear in this order |
| `args` | `run` | program arguments separated by spaces; may repeat |
| `line` | `compile-error` | the line the diagnostic points at; required |
| `code` | `compile-error` | the error code the diagnostic names; not checked with `--stage0` |

Values are taken verbatim after `key: `, without escapes. Codes are assigned when the compiler's diagnostics are built (S5); until then a test may leave `code` out.

A test's directory is its package root (§3), so a test can import modules beside it. Files and directories whose names start with `_`, and `vendor/` and `build/` directories, are not tests: `_` marks support modules, and a test package's dependencies are in `vendor/`: `tests/modules/two-modules.arche` imports `_support.shapes` from `tests/modules/_support/shapes.arche`.

## Generated tests

`tests/gen/` is written by [`gen/gen.py`](gen/gen.py), a development tool as `compiler/fuzz.py` is: families of tests, each a case of one rule, whose expected output the script computes from its own model of the rule. Integer arithmetic on edge values for every type and operator, and its traps; `as` and `as%` between every pair of integer types, and the traps of `as`; comparisons; `List` and `Map` in random sequences of operations; `String`; compile errors (a type where another is wanted, mixed operands, labels, shadowing); and, as full Arche, `std.json` round trips, `core.deflate` against zlib's CRC-32, `web`'s router, and `kv` against a dictionary, reopened on the way. The seed is fixed, so the script writes the same files; they are kept, and the suite needs no Python. Change the script, run it, and commit what it wrote.

## Compiler contract

The runner calls the compiler as `<command> <source> <output>`:

- On success it exits with 0 and leaves an executable at `<output>`.
- On a compile error it exits with a nonzero status, and the diagnostic contains `<source>:<line>:` with `<source>` exactly as it was passed in.

`stage0/arche0-build` implements this contract for stage0.

## How a test runs

- The compiler runs in the runner's directory. The program runs in the test file's directory, so it can read files that sit beside the test.
- stdin is the system's empty file (`/dev/null`, `NUL`). stdout and stderr are captured separately.
- Each step has a time limit (`--timeout`, default 10 s). On timeout, or when the step ends, everything it started is killed with it: its process group on Linux, its job on Windows.
- A test marked for another system is skipped (`system`). The runner knows the system it was built for from `std.SYSTEM`.
- `--stage0` skips tests marked `stage0: skip` and does not check `code`.
- `--codes E01,E02` runs only the compile-error tests whose `code` starts with one of the prefixes and skips the rest: for a compiler that checks programs but cannot build them yet. Codes are listed in [`docs/error-codes.md`](../docs/error-codes.md).
- Tests run in path order. The report has one `PASS`, `FAIL` or `SKIP` line per test, details under each failure, and a count line at the end. The runner exits with 1 if any test failed.

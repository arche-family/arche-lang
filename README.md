# Arche

Arche (Greek *arkhē*: origin, first principle) is a systems programming language meant to carry a whole stack — bootstrap, compiler, kernel, network and web — under one memory model and one capability model.

**Status:** self-hosting. stage0, a C11 translator, builds the Arche compiler; the compiler, written in [Arche-0](spec/arche-0.md), builds itself through LLVM into binaries without the C library, on Linux and on Windows, and stage2 and stage3 of the bootstrap are bit-identical on both. The full language ([spec/arche.md](spec/arche.md)) is being implemented sprint by sprint ([docs/sprints.md](docs/sprints.md)): full-Arche packages already have generics, compile-time evaluation, interfaces with `impl`, `where` and `&dyn`, `for` loops, closures, regions, `unsafe`, floats, contracts, `nopanic` and structured concurrency; a constant-time crypto library; TLS 1.3; an HTTP/1.1 and HTTP/2 server; and a freestanding target whose programs boot in QEMU on x86-64 and RISC-V 64 ([freestanding/](freestanding/)).

## The criterion

You should be able to tell what Arche code does from the file it is in, without reading the rest of the project. People and AI agents both read code in fragments; every rule follows from that.

- **Every name's origin is visible.** No glob imports, shadowing, function overloading or operator overloading.
- **A signature is the whole contract.** Capabilities, allocator and error set appear in it.
- **Nothing is hidden.** No implicit conversions, exceptions or hidden allocation.
- **Same-typed arguments are labeled** at the call site: `copy(from: a, to: b)`.
- **Results are used.** An ignored error is a compile error.
- **Deterministic by default.** Insertion-ordered maps, seeded randomness, reproducible builds.
- **The compiler talks back.** JSON diagnostics with machine-applicable fixes, `arche query`, `arche explain`.

Memory is value-semantic, with no garbage collector and no lifetime annotations: references are second-class, long-lived links are handles into arenas, and a web request runs in its own region that is freed in one step.

## What it looks like

```arche
import std;
import alloc;

type ConfigError = error { NotFound, AccessDenied, IsDirectory, InvalidUtf8, IoFailed, Malformed }

// `files` and `a` come from the caller: this function can read files
// and allocate memory, and nothing else.
pub fn load_config(files: &std.Fs, a: alloc.Allocator, path: str) -> ConfigError!Config {
    let text = try files.read_file(path, a);
    return Config.parse(text.view());
}
```

## Bootstrap

1. **stage0** — C11, about 5–8k lines, translates Arche-0 to C. Any C compiler (gcc, clang, tcc) builds the result. C already has a full-source bootstrap chain that starts from a few hundred bytes of hex, so Arche inherits it.
2. **stage1** — the Arche compiler, written in Arche-0, built by stage0.
3. **stage2** — the same source built by stage1, with the real backend.
4. **stage3** — built by stage2. stage2 and stage3 must be bit-identical.

## Repository

| Path | Contents | Status |
| --- | --- | --- |
| [`docs/plan.md`](docs/plan.md) | the full plan (Uzbek) | snapshot, 2026-11-05 |
| [`docs/sprints.md`](docs/sprints.md) | sprints S0–S34 to finish the language, S35 for the libraries the Arche OS phone needs, S36–S37 for editors and macOS (Uzbek) | current sprint: S28 |
| [`spec/arche-0.md`](spec/arche-0.md) | the bootstrap subset | draft 0.1 |
| [`spec/arche.md`](spec/arche.md) | the full language, built on Arche-0 | draft 0.1 |
| [`conformance/`](conformance/README.md) | specification tests | 461 tests, on Linux and Windows |
| `stage0/` | bootstrap compiler, C11 | complete: compiles all of Arche-0 |
| [`compiler/`](compiler/) | the Arche compiler, Arche-0, through LLVM, or with no LLVM by its own x86-64 backend (`build x.arche x.o`: ELF, DWARF, linear scan); `--release` runs its own optimizer; `--target` picks the system | self-hosting: stage2 == stage3, by its own backend too, in release builds and on Windows too |
| [`bench/`](bench/) | compile-time and run-time benchmarks | [baseline](bench/baseline.md) |
| [`driver/`](driver/) | `arche-driver build`/`run`: packages from `build.arche`, one object per module, cached by SHA-256; `pkg add/lock/vendor/publish` and `arche-registry`: signed, content-addressed packages over HTTPS; `arche-lsp`: a language server that runs the compiler per request | a cached build is bit-identical to a clean one; tampered packages are refused; a real editor session passes, with diagnostics under a second on the compiler's source |
| [`editors/`](editors/README.md) | TextMate grammar and how to use `arche-lsp` | the grammar's keywords are compared with the lexer's |
| [`docs/abi.md`](docs/abi.md) | layouts, calling convention, runtime | version 0 |
| [`docs/error-codes.md`](docs/error-codes.md) | every diagnostic code | living |
| [`lib/`](lib/) | the library: `core` (no OS, no allocator), `alloc`, `std` for Linux and Windows, `crypto` (constant time, checked in CI by [`ctcheck/`](ctcheck/)), `tls` (TLS 1.3 and X.509) and `http` (HTTP/1.1 and HTTP/2, server and client), interop-tested against OpenSSL, curl and Chrome by [`interop/`](interop/) | the compiler bootstraps on `lib/alloc` and `lib/std` too |

## Related

[arche-os](https://github.com/arche-family/arche-os) — a capability-based microkernel operating system written in Arche. Its first product is a managed phone, and its application model is [Nimadir](https://github.com/Nodirbek-Abdulaxadov/nimadir)'s: WASM components on a WIT contract, which is why the WASM runtime here implements the Component Model.

## License

Not chosen yet.

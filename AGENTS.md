# Arche — brief for agents and contributors

Read this before you change anything in `arche-family/arche-lang` or `arche-family/arche-os`. It is written for AI agents and for people; both should be able to start work from it alone.

## Origin

Arche began as a question to Claude, an AI system made by Anthropic: *if the current era had to start from zero — no C, C++, Rust or LLVM — which language would you start from, and what language would you write for systems, operating systems, web and everything else?* The follow-up settled the terms: *the language is yours; every decision is yours; make it the language that is convenient for you and optimal in every respect.*

So Claude designed the language, its toolchain and its operating system, and the repositories belong to Nodirbek, who owns the `arche-family` organization. "Convenient for Claude" turned into one design criterion, stated below. It is not a style preference: an AI agent always reads code in fragments, and so do most people.

## The criterion

**What a piece of Arche code does must be clear from the file it is in, without reading the rest of the project.** Every language rule serves this and removes a specific class of mistakes:

| Rule | Mistake it removes |
| --- | --- |
| No glob imports, shadowing, function or operator overloading | calling a name that does not exist or means something else |
| Capabilities, allocator and error set appear in the signature | unexpected I/O, allocation or failure inside a call |
| No implicit conversions, exceptions or hidden allocation | invisible cost and control flow |
| Same-typed parameters are labeled at the call site | swapped arguments |
| An unused result, including an ignored error, is a compile error | errors that disappear silently |
| Insertion-ordered maps, seeded randomness, reproducible builds | flaky tests and bugs that cannot be reproduced |
| One format, one generics mechanism, a short spec | dialects and partial knowledge of the language |
| JSON diagnostics with machine-applicable fixes, `arche query` | misread errors, getting lost in a codebase |

## Where things are

| Path | What it is | Status |
| --- | --- | --- |
| `arche-lang/README.md` | the language in one page | current |
| `arche-lang/spec/arche-0.md` | the bootstrap subset; complete enough to implement stage0 | draft 0.1 |
| `arche-lang/docs/plan.md` | full language plan, in Uzbek | snapshot, 2026-11-03 |
| `arche-os/README.md` | the OS in one page | current |
| `arche-os/docs/plan.md` | full OS plan, in Uzbek | snapshot, 2026-11-03 |

Nothing runs yet. The plans are snapshots; the specification is the source of truth for the language.

## Fixed decisions

Do not change these without changing the specification first, in the same pull request, with the reason written down.

**Language**

- Name **Arche**; files `.arche`; one toolchain binary, `arche` (`build`, `run`, `test`, `check --json`, `fix`, `query`, `explain`, `fmt`, `doc`, `pkg`).
- Value semantics with move and copy. No garbage collector, no lifetime annotations.
- References and slices (`&T`, `&mut T`, `[]T`, `str`) are second-class views: parameters and locals only. A function may return a view only when exactly one parameter is a view (the projection rule). Long-lived links are handles into arenas; a web request runs in a region freed in one step.
- Allocators are passed explicitly. Capabilities (`Fs`, `Net`, `Clock`, `Env`, `Proc`) arrive only as parameters; there is no global I/O and no global mutable state.
- Errors are values: `E!T`, `try`, `catch`, `orelse`, return traces. No exceptions. `?T` instead of null.
- Checked arithmetic (overflow traps), `+%` to wrap, `as` checked and `as%` wrapping conversions, bounds checks always on.
- Generics, macros and build logic are one mechanism: `comptime` with `where` constraints.
- ASCII identifiers; `and`, `or`, `not`; every block in braces; trailing commas everywhere; no significant whitespace.
- A stable ABI from day one, with slices and error unions as ABI types.
- Every package declares an `edition`; `arche fix --edition` migrates code.

**Bootstrap and compiler**

- stage0 is written in C11, about 5–8k lines, and translates Arche-0 to C that gcc, clang or tcc compile. C's full-source bootstrap chain, which starts from a few hundred bytes of hex, thereby covers Arche.
- The compiler (stage1) is written in Arche-0 and stays in Arche-0. stage2 and stage3 must be bit-identical.
- The compiler has its own SSA IR. LLVM is used only as a release backend, through textual `.ll`, behind that IR. Debug and WASM backends are Arche's own.

**Operating system**

- A capability-based microkernel: memory, scheduling, IPC, interrupts and capabilities in the kernel; drivers and servers in userspace, restarted on crash.
- No POSIX, no `fork`, no signals, no global namespace. Processes are created with `spawn(image, capabilities)` and see only the `Directory` their parent assembles.
- Synchronous IPC with a register fast path; Ports for asynchronous events.
- x86-64 and RISC-V 64; QEMU first, then one reference machine.

## Next work order

**stage0, in `arche-lang/stage0/`.** The whole compiler for Arche-0 as `spec/arche-0.md` defines it:

- lexer, parser, name resolution, and enough type checking to lower every construct;
- a C11 emitter, plus a runtime in C for the built-ins, `alloc` and `std` surface of spec §11 and the traps of spec §12;
- a command line: `stage0 <root-dir> -o <out.c>`, where `<root-dir>` contains `main.arche`;
- tests: every example in the spec, and at least one passing and one failing program for each rule stage0 enforces.

It is done when every example in the spec compiles and runs, the generated C compiles without warnings under `gcc` and `clang` with `-std=c11 -Wall -Wextra`, the same input always produces byte-identical C, and stage0 has no dependency beyond the C11 standard library.

After stage0 comes the compiler in Arche-0, then `core`, `alloc` and `std`. The OS starts when the compiler offers a `freestanding` target.

## Working rules

- **Spec first.** Read `spec/arche-0.md` before writing stage0 or compiler code. When code and spec disagree, the spec wins; when the spec is wrong, fix it in the same pull request and say why.
- **English** for code, comments, specifications, READMEs, commit messages and pull requests. The plans in `docs/` stay in Uzbek.
- **Tests with every rule:** at least one program that must compile and one that must be rejected.
- **Determinism:** no timestamps, absolute paths or hash-order dependence in any output.
- **Small and dependency-free:** stage0 uses only the C11 standard library; the compiler uses only Arche-0.
- **Pull requests** say what changed, why, and how it was verified. One coherent change per pull request.
- **Ask the owner first** before changing a fixed decision, adding a dependency, renaming anything public, or choosing a license (none is chosen yet).

## Prompt for a new session

Copy this into a new agent session and fill in the task:

```text
You are continuing Arche: a programming language, its toolchain and a
capability-based microkernel OS, designed from first principles by Claude.
The repositories are arche-family/arche-lang and arche-family/arche-os.

Before doing anything, read AGENTS.md, spec/arche-0.md and README.md in
arche-lang; for OS work also read arche-os/README.md and
arche-os/docs/plan.md. Keep every decision listed under "Fixed decisions"
in AGENTS.md. If the task requires changing one, change the spec in the
same pull request and state the reason.

Task: <describe the task, or "the next work order in AGENTS.md">

Deliver the work as a pull request to main, with tests. In the
description, say what changed, why, and how you verified it.
```

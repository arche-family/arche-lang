# Nimadir's mini-apps, as built

Five components of [Nimadir](https://github.com/Nodirbek-Abdulaxadov/nimadir)
(commit ee1e1ab, `mini-apps/*/*.component.wasm`), kept here as they were built
so that `nimadir-check.sh` runs them unrebuilt: `counter`, `hello`, `gallery`
and `store` are apps, `runaway` is the shell's test of its limits. They are
Rust, compiled to `wasm32-unknown-unknown` and made components by
`wit-component`, against `wit/world.wit` (`nimadir:shell@1.2.0`).

`world.wit` is its contract, `nimadir:shell@1.2.0`, as Nimadir has it.
`registry.list` is Nimadir's registry at that commit (`apps.json`, in the
`apps.list` form). `cases.txt` names the cases of Nimadir's `run-tests.sh` that
drive these apps headlessly, and `expected/` holds what Nimadir's own shell
said to each, normalized by `normalize.sh`; `make-expected.sh` writes them
again from a Nimadir checkout. `nimadir-check.sh` holds arche-wasm to them.

Nimadir is under the MIT License, Copyright (c) 2026 Nodirbek Abdulaxadov.

# Nimadir's mini-apps, as built

Eight components of [Nimadir](https://github.com/Nodirbek-Abdulaxadov/nimadir)
(commit ee1e1ab, `mini-apps/*/*.component.wasm`), kept here as they were built
so that `nimadir-check.sh` runs them unrebuilt: `counter`, `hello`, `gallery`,
`store` and `todo` are apps, `runaway` is the shell's test of its limits, and
`probe` its fixture for CSS: a fixed page, and a field whose text becomes the
page's stylesheet (`--styles --type "1:2=<sheet>"`). They are Rust, compiled to
`wasm32-unknown-unknown` and made components by `wit-component`, against
`wit/world.wit` (`nimadir:shell@1.2.0`). `counter-cs` is the counter in C#,
built by .NET NativeAOT: it brings .NET's runtime, which imports WASI 0.2
(streams, clocks, environment, exit, random, filesystem) and starts from a core
start function.

`world.wit` is its contract, `nimadir:shell@1.2.0`, as Nimadir has it.
`registry.list` is Nimadir's registry at that commit (`apps.json`, in the
`apps.list` form). `cases.txt` names the cases of Nimadir's `run-tests.sh` that
drive these apps headlessly (with its section "CSS (ADR-4)" and more cases of
the cascade, the arguments read as the shell reads them, quotes and all), and
`expected/` holds what Nimadir's own shell said to each, normalized by
`normalize.sh`; `make-expected.sh` writes them again from a Nimadir checkout.
`nimadir-check.sh` holds arche-wasm to them.

Nimadir is under the MIT License, Copyright (c) 2026 Nodirbek Abdulaxadov.

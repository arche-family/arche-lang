# Nimadir's mini-apps, as built

Four components of [Nimadir](https://github.com/Nodirbek-Abdulaxadov/nimadir)
(commit ee1e1ab, `mini-apps/*/*.component.wasm`), kept here as they were built
so that `nimadir-check.sh` runs them unrebuilt: `counter`, `hello` and `store`
are apps, `runaway` is the shell's test of its limits. They are Rust, compiled
to `wasm32-unknown-unknown` and made components by `wit-component`, against
`wit/world.wit` (`nimadir:shell@1.2.0`).

Nimadir is under the MIT License, Copyright (c) 2026 Nodirbek Abdulaxadov.

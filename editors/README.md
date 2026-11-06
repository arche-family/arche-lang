# Editors

- `arche.tmLanguage.json`: a TextMate grammar (scope `source.arche`, files `*.arche`). `check-grammar.py`, run by `make -C compiler check`, compares its keywords with the lexer's.
- `arche-lsp` (`driver/lsp.arche`, built by `make -C driver`): a language server over stdin and stdout, with `didOpen`, `didChange`, `didClose`, `didSave`, `publishDiagnostics`, `definition`, `references`, `hover`, `completion`, `formatting` and quick fixes through `codeAction`.

Point an editor's LSP client at `arche-lsp` for files of the `arche` language. It takes no arguments; it finds the compiler through `ARCHE`, beside itself, in this repository's layout (`driver/build/` next to `compiler/build/`), or in `PATH`.

Each request runs the compiler as its own process, with the editor's unsaved text passed by `--overlay`, so there is nothing in the server to go stale. A file belongs to the package of the nearest `package.arche` above it, and a change re-checks the package's open files.

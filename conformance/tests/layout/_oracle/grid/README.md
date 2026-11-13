# Grid oracle

`_tree_grid.arche`'s tree and case language in Rust, over the real taffy 0.15 (`taffy = "=0.15.0"`, default features), so the grid tests' expected lines are taffy's own. It builds offline from the local Cargo registry: `cargo build --offline --release`.

- `python3 gen.py` builds the oracle, writes the seeded random cases to `cases.txt`, runs them and writes `../../grid_fuzz*.arche` with taffy's lines as the expected output; the same seeds give the same files.
- `python3 gen.py soak SEED COUNT [LEAN]` runs COUNT more cases from another seed through both the oracle and the Arche port (`ARCHE_FLAGS=--release` for a release build) and prints those that differ; nothing is kept.
- `python3 existing.py` translates the hand-written `../../grid_*.arche` cases into Rust (`src/existing.rs`) and checks their expected lines against taffy.

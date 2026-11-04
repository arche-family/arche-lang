# Error codes

Every diagnostic of the Arche compiler has a stable code. A code never changes meaning; a retired code is never reused. Conformance tests name the code they expect (`// code: E0112`), and `arche explain <code>` prints its entry.

| Range | Phase |
| --- | --- |
| `E01xx` | lexical: source text and tokens (spec §2) |
| `E02xx` | syntax (§14) |
| `E03xx` | names, modules and visibility (§3, §4, §6) |
| `E04xx` | types and expressions (§5, §7) |
| `E05xx` | statements, patterns and paths (§8, §9, §10) |
| `E06xx` | the memory model: views and moves (§5.4, §5.5) |
| `E07xx` | constants (§4) |

## Lexical

| Code | Meaning |
| --- | --- |
| `E0101` | The file is not valid UTF-8. |
| `E0102` | A `\r` outside a string literal; lines end with `\n`. |
| `E0103` | A character that starts no token, such as `#` or a non-ASCII letter outside a string or comment. |
| `E0104` | A word reserved for full Arche (§2.1) used as a name. |
| `E0110` | An integer literal with a base prefix and no digits: `0x`. |
| `E0111` | A `_` in an integer literal that does not separate two digits. |
| `E0112` | A digit that is not valid in the literal's base, or a suffix: `0b102`, `10u64`. |
| `E0113` | An integer literal that does not fit in 64 bits. |
| `E0120` | An empty character literal: `''`. |
| `E0121` | A character literal with more than one character. |
| `E0122` | A non-ASCII character literal. |
| `E0123` | A character literal that is not closed. |
| `E0130` | A string literal that is not closed on its line. |
| `E0131` | An unknown escape: `\q`. |
| `E0132` | A `\x` escape above `\x7F`. |
| `E0133` | A `\u{...}` escape in a character literal. |
| `E0134` | A malformed `\u{...}` escape: no digits, more than six, or no closing `}`. |
| `E0135` | A `\u{...}` escape that is not a Unicode scalar value. |

## Syntax

| Code | Meaning |
| --- | --- |
| `E0201` | An unexpected token: the grammar needs something else here. |
| `E0202` | An `import` after another item; imports come first. |
| `E0203` | Chained comparisons: `a < b < c`. |
| `E0204` | `&`, `^` and `|` mixed without parentheses. |
| `E0205` | `and` and `or` mixed without parentheses. |
| `E0206` | A struct field after a method; fields come first. |
| `E0207` | A module-level `var` or `let`; a module has no mutable state. |
| `E0208` | A field in an enum body; an enum lists variants, then methods. |
| `E0209` | A `_ = ...` discard as a `while` step. |
| `E0210` | A variant written with `{ }` and no fields. |
| `E0211` | `pub` before `test` or `import`. |

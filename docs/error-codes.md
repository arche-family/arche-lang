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

## Names, modules and visibility

| Code | Meaning |
| --- | --- |
| `E0301` | An unknown name where a value is expected. |
| `E0302` | An unknown type. |
| `E0303` | An unknown function. |
| `E0304` | An item, parameter, field, variant or error declared twice. |
| `E0305` | A name declared while the same name is visible: Arche has no shadowing (§6.3). Erasable. |
| `E0306` | A private item, field or method used from another module (§4). Erasable. |
| `E0307` | An imported module that cannot be found. |
| `E0308` | A name imported twice, or an import of a library module Arche-0 does not have. |
| `E0309` | A name that is not a value: a function, type or module used as one. |
| `E0310` | A field, method, variant or error that the type does not have. |
| `E0311` | A module that has no item with this name. |
| `E0312` | A method with the name of a field or variant of its type. |
| `E0313` | No `main`, or `main` not declared as `fn main(sys: &mut std.System) -> !void`. |

## Types and expressions

| Code | Meaning |
| --- | --- |
| `E0401` | A value of the wrong type. |
| `E0402` | The operands of a binary operator have different types. |
| `E0403` | An operator that does not work on this type. |
| `E0404` | An integer literal that does not fit in its type. |
| `E0405` | An integer literal, `none` or array literal with no type from context (§5.6). Erasable for integers. |
| `E0406` | An `as` conversion between types that are not both integers, or of a literal. |
| `E0407` | A call with the wrong number of arguments. |
| `E0408` | A label that does not match its parameter (§7.5). Erasable. |
| `E0409` | A missing label where two parameters have the same type (§7.5). Erasable. |
| `E0410` | An expression that cannot be called. |
| `E0411` | An assignment or `&` of something that is not a place. |
| `E0412` | A change through a place that is not mutable: a `let`, a parameter, or a `&` view. |
| `E0413` | `.*` of something that is not a reference. |
| `E0414` | Indexing or slicing a type that cannot be indexed, or slicing a value that is not a place. |
| `E0415` | A struct literal whose fields are missing, unknown or out of order. |
| `E0416` | A variant used without its fields, or with fields it does not have. |
| `E0417` | An array literal with the wrong number of elements. |
| `E0418` | `orelse` or `catch` on a value that is not an optional or error union. |
| `E0419` | `try` in a function that does not return an error union, or on a value that is not one. |
| `E0420` | An error that the function's error set does not contain. |
| `E0421` | `let` of an expression that has no value. |
| `E0422` | A method whose `self` is not the type, `&` or `&mut` of it; or an associated function called as a method. |
| `E0423` | A struct or enum that contains itself. |
| `E0424` | A type alias that refers to itself. |
| `E0425` | A Map key type that is not a key type (§5.5). |
| `E0426` | A generic type with the wrong number of arguments, or one Arche-0 does not have. |

## Statements, patterns and paths

| Code | Meaning |
| --- | --- |
| `E0501` | A result that is not used; write `_ = ...` to drop it (§7.8). Erasable. |
| `E0502` | `return` without a value in a function that returns one, or with one in a function that does not. |
| `E0503` | A function that can reach its end without returning its value (§10). |
| `E0504` | `break` or `continue` outside a loop. |
| `E0505` | `return`, `break`, `continue` or `try` in deferred code. |
| `E0506` | A pattern that cannot match the type of the value. |
| `E0507` | Alternatives joined with `|` that bind names. |
| `E0508` | A variant pattern that does not name every field and does not end with `..`. |
| `E0509` | A `match` that is not exhaustive. Erasable: stage0 traps at run time instead. |
| `E0510` | An empty range pattern. |
| `E0511` | A label that no enclosing loop has, or one that repeats an enclosing loop's (spec/arche.md §3.4). |
| `E0512` | An `if` expression without `else`, or a branch of an `if` or `match` expression that ends without a value (spec/arche.md §3.1). |

## Constants

| Code | Meaning |
| --- | --- |
| `E0701` | An expression that is not a constant expression. |
| `E0702` | A constant expression that overflows its type. |
| `E0703` | Division by zero in a constant expression. |
| `E0704` | A constant that depends on itself. |
| `E0705` | A constant whose type is not an integer type, `bool` or `str`. |

## The memory model

| Code | Meaning |
| --- | --- |
| `E0601` | A view where views cannot be: a field, a payload, an element, or the payload of `?` or `!` (§5.4). |
| `E0602` | A function that returns a view with more than one view parameter (§5.4). |
| `E0603` | A view that outlives what it views: a view of a local returned, or stored in a binding of an enclosing block (§5.4). |
| `E0604` | A place used while a view of it is live, through another path: written or viewed `&mut` while viewed, or used at all while viewed `&mut` (§5.4). |
| `E0605` | A move-only value used after it was moved (§5.5). |
| `E0606` | A move out of a place reached through a view (§5.5). |
| `E0607` | A view chosen by an `if` or `match` expression whose branches view different places (spec/arche.md §3.1). |

## Packages, generics and interfaces

Reserved for the full language (spec/arche.md); assigned as the compiler implements each chapter.

| Code | Meaning |
| --- | --- |
| `E0801` | A capability type that the package manifest does not list in `needs` (§1.3). |
| `E0810` | A `for` loop over a value that is not a range, a slice or an iterator (§3.2). |
| `E0811` | A `for` loop's range or names that do not fit: bounds of two types, a pair over a range or over items that are not a struct of two fields, a type on a name that is not a range's (§3.2). |
| `E0820` | An `impl` of something that is not an interface, or for a type that cannot implement one here (§5.1). |
| `E0821` | An `impl` outside both the module that declares the type and the one that declares the interface (§5.1). |
| `E0822` | An `impl` whose methods are not the interface's: one missing, one more, one with other parameters, names or result, or one the type has already (§5.1). |
| `E0823` | A second `impl` of one interface for one type (§5.1). |
| `E0824` | A method call whose name two interfaces of the type have; name the interface: `I.name(&x, …)` (§5.1). |
| `E0825` | An interface where a type of values is expected; a view of a value that implements it is `&dyn I` (§5.3). |
| `E0830` | A `comptime` type argument that does not implement an interface its `where` bound requires (§5.2). |
| `E0831` | `dyn I` of an interface whose methods do not all take `self` by reference and use `Self` nowhere else (§5.3). |
| `E0832` | A value converted to `&dyn I`, or passed to `I.name(…)`, whose type does not implement `I` (§5.1, §5.3). |
| `E0840` | Compile-time evaluation that runs too many steps or recurses too deep (§4.3). |
| `E0850` | A closure body that uses a binding from outside without capturing it (§6). |
| `E0851` | A capture that names no local binding, or one binding twice (§6). |

## Regions, contracts, concurrency and unsafe

| Code | Meaning |
| --- | --- |
| `E0901` | A value bound to a region that leaves the region's block (§7). |
| `E0902` | A `&mut` view captured by a task while another task or its scope uses the place (§9). |
| `E0910` | An operation that can panic or trap in a `nopanic` function, or a call of a function that is not `nopanic` (§8.2). |
| `E0920` | An `unsafe` block without a `// SAFETY:` comment, in a package that sets `strict_unsafe` (§10.1). |
| `E0921` | An operation that needs `unsafe` outside an `unsafe` block or `unsafe fn`: a raw pointer made, read, written, offset or turned into a view, or an `unsafe fn` called (§10.1). |
| `E0922` | An `asm` operand that is not `in`, `out`, `inout` or `clobber`, whose value is not an integer, `bool` or pointer, that writes a place it cannot, or a `%N` without an operand (§10.2). |
| `E0923` | A `naked fn` with parameters or a result, or whose body is not one `asm` statement (§10.2). |
| `E0924` | A foreign function with another calling convention than `"c"`, or one that takes or returns something other than integers, `bool`s, floats and raw pointers (§10.1). |
| `E0925` | An `align(N)` whose `N` is not a power of two from 1 to 4096, or one on a field of a packed struct (§10.2). |
| `E0926` | A view of a field of a packed struct, which may be unaligned (§10.2). |
| `E0927` | An atomic operation whose ordering is not written as a constant, is one the operation cannot have, or whose type it does not work on (§10.2). |
| `E0928` | A freestanding program without a panic handler, or one whose handler is not `fn(message: str, file: str, line: u32, column: u32) -> noreturn`, or that is not full Arche (§10.3). |
| `E0929` | A test block's parameter that is not a `&mut std.testing.Test` (spec/arche.md §1.3, tests). |

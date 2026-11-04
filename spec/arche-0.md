# Arche-0

**Status:** draft 0.1 · 2026-11-03

Arche-0 is the bootstrap subset of Arche. The Arche compiler (`stage1`) is written in Arche-0, and `stage0` compiles nothing but Arche-0. This document is complete for the subset: a stage0 implementation needs nothing else.

## 1. Two invariants

1. **Strict subset.** Every valid Arche-0 program is a valid Arche program and means the same thing. The compiler source therefore builds unchanged with stage0, stage1 and stage2.
2. **Erasable checks.** Some rules only reject programs; they never change what a valid program does. stage0 may skip them and the full compiler enforces them. They are listed in §12.

What Arche-0 leaves out, and why the compiler does not need it, is listed in §13.

## 2. Source text

- Files are UTF-8 with the extension `.arche`. Lines end with `\n`; a `\r` outside a string literal is an error (`arche fmt` normalizes).
- Whitespace is space, tab and `\n`. It separates tokens and means nothing else.
- Identifiers are ASCII: `[A-Za-z_][A-Za-z0-9_]*`. No Unicode identifiers, so there are no look-alike names and a search for a name finds exactly that name. A lone `_` is not an identifier; it is the discard.
- `//` starts a comment that runs to the end of the line. `///` is a doc comment and attaches to the next item, field, variant or method. There are no block comments.

### 2.1 Keywords

```
and     as      break   catch   const   continue  defer   else
enum    err     error   false   fn      if        import  let
match   mut     none    not     ok      or        orelse  pub
return  some    struct  test    true    try       type    var
while
```

Reserved for full Arche and rejected as identifiers here too, so Arche-0 code stays forward compatible:

```
asm  comptime  dyn  ensures  for  in  interface  naked  nopanic
region  requires  unsafe  where
```

The primitive type names (§5.1) are predeclared names, not keywords. Like every name, they cannot be redeclared (§6.3).

### 2.2 Literals

| Literal | Examples | Type |
| --- | --- | --- |
| Integer | `0`, `42`, `1_000`, `0xFF`, `0o17`, `0b1010` | from context (§5.6) |
| Character | `'a'`, `'\n'`, `'\x7F'` | `u8`, ASCII only |
| String | `"text\n"` | `str` |
| Boolean | `true`, `false` | `bool` |
| Optional | `none` | `?T` from context |

- `_` may separate digits, never lead or trail. There are no type suffixes.
- Escapes in character and string literals: `\n \r \t \0 \\ \" \' \xHH`. In strings `\xHH` is limited to `\x00`–`\x7F` so every `str` stays valid UTF-8; strings also accept `\u{H…}` for any Unicode scalar value. A string literal may not span lines.

### 2.3 Operators and punctuation

```
+   -   *   /   %   +%  -%  *%
&   |   ^   ~   <<  >>
==  !=  <   <=  >   >=
=   +=  -=  *=  /=  %=  +%=  -%=  *%=  &=  |=  ^=  <<=  >>=
.   ,   :   ;   ->  =>  ..  ..=  .*  ?   !   _
(   )   [   ]   {   }
```

`as%` is one token: the keyword `as` immediately followed by `%`.

## 3. Modules

- One file is one module. A package has a root directory, and a module's path is its file path relative to that root, without the extension, with `/` written as `.`: `syntax/lexer.arche` is `syntax.lexer`. The root module (§10) can be imported by its path like any other.
- `import syntax.lexer;` binds the last segment, `lexer`, in the file. `import syntax.lexer as lex;` binds `lex`.
- Imports come first in a file, one per line.
- There are no glob imports and no selective imports. A name from another module is always written qualified: `lexer.Token`.
- The roots `core`, `alloc` and `std` are reserved. In Arche-0 they are provided by the compiler (§11).
- Import cycles are allowed; a program is compiled as a whole.

## 4. Items

```arche
/// Deepest block nesting the parser accepts.
const MAX_DEPTH: u32 = 256;

type Index = u32;                      // alias: Index and u32 are one type

type NodeId = struct { index: u32 }    // one-field struct: a typed handle

type Span = struct {
    pub start: u32,
    pub len: u32,

    pub fn end(self: Span) -> u32 {
        return self.start + self.len;
    }
}

type TokenKind = enum {
    Ident,
    Int,
    Plus,
    Eof,
}

type Expr = enum {
    Int { value: u64 },
    Binary { op: TokenKind, lhs: NodeId, rhs: NodeId },
}

type ParseError = error { UnexpectedToken, UnexpectedEof }

test "span end" {
    let s = Span { start: 3, len: 4 };
    assert(s.end() == 7);
}
```

- **`const`** takes a constant expression: literals, other constants, arithmetic, comparisons, `and`/`or`/`not` and `as` between integer types. A constant's type is an integer type, `bool` or `str`.
- There is no module-level `var`. A program has no global mutable state.
- `type X = struct {…}`, `type X = enum {…}` and `type X = error {…}` end with `}`. An alias, `type X = T;`, ends with `;`.
- A struct body lists its fields first, then its methods. Every field is followed by `,` unless it is the last member before `}`. An enum body lists its variants first (comma-separated, trailing comma allowed), then its methods.
- A function declared inside a type body is an associated function, called as `Type.name(…)`. If its first parameter is named `self` and has type `T`, `&T` or `&mut T`, where `T` is the enclosing type, it can also be called as a method: `x.name(…)` (§7.5).
- Everything is private to its module unless marked `pub`. Struct fields carry their own `pub`. Enum variants and error names are public when their type is.
- `test "name" {…}` declares a test.

## 5. Types

### 5.1 Primitive types

| Type | Meaning |
| --- | --- |
| `i8` `i16` `i32` `i64` | signed, two's complement |
| `u8` `u16` `u32` `u64` | unsigned |
| `isize` `usize` | pointer-sized; 64-bit on every Arche-0 target |
| `bool` | `true` or `false` |
| `void` | no value; a function without `->` returns `void` |
| `noreturn` | the type of expressions that never complete: `panic(…)`, `return`, `break`, `continue` |
| `str` | a view of valid UTF-8 bytes (§5.4) |

### 5.2 Composite types

- **`struct`**: named fields; a value type.
- **`enum`**: a tagged union. A variant has no payload or named fields, `V { a: A, b: B }`. There are no positional (tuple) variants.
- **`error { A, B }`**: a set of error names. Error names are global: `ParseError.UnexpectedEof` and `IoError.UnexpectedEof` are the same value.
- **`[N]T`**: a fixed-size array; a value type. `N` is a constant expression.
- **`?T`**: either `none` or `some(x)`.
- **`E!T`**: either an error from the set `E` or a `T`. `!T` lets the compiler infer the set; it is allowed only on functions that are not `pub`.

### 5.3 Built-in generic types

Arche-0 cannot declare generic types or functions. It can use these, which the library provides (§11). In expressions they are written with their arguments too: `alloc.List(u32).init(sys.alloc)`.

| Type | Purpose |
| --- | --- |
| `alloc.List(T)` | growable array |
| `alloc.Map(K, V)` | hash map; `K` is a key type and `V` is copyable (§5.5); iteration follows insertion order |
| `alloc.String` | growable UTF-8 text |
| `alloc.Interner` | string interning, `str` ↔ `alloc.Symbol` |
| `alloc.Allocator` | allocator handle |

### 5.4 Views and the projection rule

`&T`, `&mut T`, `[]T`, `[]mut T` and `str` are **views**: they refer to memory owned by something else. Arche has no lifetime annotations. Views are second-class instead, and four rules make that sufficient:

1. **Where a view may appear.** A view may be the type of a parameter or of a local `let`/`var`. A `const` may have type `str`.
2. **Where it may not.** A view may not be a struct field, an enum payload, an array element, a `List` element, a `Map` key or value, or the payload of `?` or `!`.
3. **Projection.** A function may return a view only if exactly one of its parameters is a view, counting `self`. The result is a projection of that argument and lives no longer than it. Slicing, `x[a..b]`, is a projection of `x`.
4. **Exclusivity.** While a `&mut` view of a place is live, that place cannot be read, written or viewed through any other path. Places are tracked by path, so a view of `sys.args` and a `&mut` view of `sys.stdout` can be live together.

A `&mut T` is accepted where a `&T` is expected, and a `[]mut T` where a `[]T` is: a view can give up the right to write, never gain it.

Because a reference is never stored, there is nothing to annotate. Long-lived links between objects are handles (`NodeId`, `alloc.Symbol`) into lists. String literals are views of static memory and satisfy every rule.

### 5.5 Copy, move and equality

A type is **copyable** if it is a primitive, a view, an error set, `alloc.Allocator`, `alloc.Symbol`, or a struct, enum, array or optional built only from copyable types. Every other type is **move-only**: containers, capabilities, and anything that contains one.

- Assigning or passing a move-only value moves it. The source cannot be used again until it is reassigned.
- Views are copyable but stay bound by §5.4.
- **Key types**, usable as `Map` keys: integers, `bool`, payload-free enums, `alloc.Symbol`, and structs made only of key types.
- `==` and `!=` work on integers, `bool`, payload-free enums, error values, `alloc.Symbol`, and structs of these, compared field by field. They do not work on views, containers or enums with payloads. Text is compared with `a.eq(b)`, a visible O(n) call.

### 5.6 Integers

- There are no implicit conversions between integer types, or between integers and `bool`.
- An integer literal takes the type its context requires: the declared type of a `let`/`var`, a parameter, a field, a return type, or the other operand of a binary operator. Without context it is an error: `let x = 5;` must be `let x: u32 = 5;`.
- A literal shift count, as in `x << 3`, has type `u32`.
- `-` written directly before an integer literal makes a negative literal, so `let x: i8 = -128;` is valid.
- `x as T` converts between integer types and traps if the value does not fit in `T`.
- `x as% T` converts and wraps, keeping the low bits.

## 6. Bindings and scopes

### 6.1 `let` and `var`

`let` binds an immutable name, `var` a mutable one. Both must be initialized; the type may be omitted when the initializer determines it. Parameters are immutable bindings.

### 6.2 Discard

`_ = expr;` evaluates `expr` and drops the result. It is the only way to ignore a non-`void` result (§7.8).

### 6.3 No shadowing

A name cannot be declared while the same name is visible: from an enclosing block, a parameter, a module-level item, an import or a predeclared name. Sibling blocks and different functions may reuse a name.

## 7. Expressions

### 7.1 Precedence

| Level (high to low) | Operators |
| --- | --- |
| postfix | `f(…)` `.name` `x[i]` `x[a..b]` `.*` `T {…}` |
| prefix | `-` `~` `not` `try` `&` `&mut` |
| conversion | `as` `as%` |
| multiplicative | `*` `/` `%` `*%` |
| additive | `+` `-` `+%` `-%` |
| shift | `<<` `>>` |
| bitwise | `&` `^` `\|` |
| comparison | `==` `!=` `<` `<=` `>` `>=` |
| and | `and` |
| or | `or` |
| fallback | `orelse` `catch` |

Rules that remove ambiguity:

- Bitwise operators bind tighter than comparison: `x & MASK == 0` means `(x & MASK) == 0`.
- `&`, `^` and `|` cannot be mixed without parentheses.
- `and` and `or` cannot be mixed without parentheses.
- Comparisons do not chain: `a < b < c` is an error.
- Operators from multiplicative to bitwise are left-associative. `orelse` and `catch` are right-associative.
- `try` applies to the whole postfix chain after it: `try f(x).y` is `try (f(x).y)`. Write `(try f(x)).y` to use a field of the result.
- Unary `-` on an unsigned type is an error.

### 7.2 Arithmetic

- `+ - * / %` are checked: overflow and division or remainder by zero trap.
- `/` truncates toward zero; the result of `%` has the sign of the dividend.
- `+% -% *%` wrap.
- In `a << b` and `a >> b`, `b` is any unsigned type and must be less than the bit width of `a`, or the shift traps. `<<` shifts the two's complement bits and drops those shifted out; it does not trap on overflow. `>>` is arithmetic on signed types and logical on unsigned ones.
- Except for shifts, both operands of a binary operator have the same type.

### 7.3 Evaluation order

Always left to right: operands, arguments, struct-literal fields, array elements. In an assignment the place is evaluated before the value, and a compound assignment evaluates its place once.

### 7.4 Constructing values

- Struct: `Span { start: 3, len: 4 }`, every field, in declaration order.
- Enum: `Expr.Binary { op: TokenKind.Plus, lhs: a, rhs: b }`, `TokenKind.Plus`. Arche-0 always writes the type name.
- A struct literal cannot be the condition of `if` or `while` or the scrutinee of `match` unless it is in parentheses.
- Array: `[1, 2, 3]`; the element type comes from context.
- Optional: `some(x)`, `none`.

### 7.5 Calls and labels

- Forms: `f(a, b)`, `Type.f(a)`, `x.method(a)`.
- **Method calls.** `x.m(args)` calls `T.m(x, args)`, passing `x` by value, as `&x` or as `&mut x`, as the method's `self` declares. If `self` is by value and `T` is move-only, `x` is moved.
- **Explicit views in free calls.** Outside method syntax, a view argument is written out: `measure(&list)`, `fill(&mut buf)`. A mutation is always visible at the call site.
- **Labels.** Any argument may carry its parameter's name: `copy(from: a, to: b)`. Labels must match the parameter names and appear in parameter order. A label is **required** when two or more parameters, not counting `self`, have the same declared type; then every argument for those parameters is labeled. Declared types are compared as written, before generic substitution.

### 7.6 Places

A place is a local binding, a field of a place (`p.f`), an element of a place (`p[i]`), or `r.*` for a reference `r`. Only places can be assigned to or viewed with `&`/`&mut`. Field access through a reference dereferences automatically: `r.f` is `r.*.f`. `list[i]` on an `alloc.List(T)` is a place, so `list[i].count += 1` changes the element in the list.

### 7.7 Indexing and slicing

- `x[i]` works on `[N]T`, `[]T`, `str` and `alloc.List(T)`. `i` is a `usize`; an index out of range traps. Indexing a `str` gives a byte, `u8`.
- `x[a..b]` is a view of elements `a` up to but not including `b`; it traps unless `a <= b <= x.len()`. Slicing a `str` also traps unless both ends fall on UTF-8 character boundaries. Slicing a mutable place, or a `[]mut T`, gives a `[]mut T`; anything else gives a `[]T`.
- `x.len()` gives the length as `usize`.

### 7.8 Optionals and errors

- `x orelse y`: `x` is `?T`; the result is `T`; `y` is a `T` or `noreturn`.
- `try x`: `x` is `E!T`; on an error, the enclosing function returns that error. Its error set must contain `E` or be inferred.
- `x catch y`: `x` is `E!T`; the result is `T`; `y` is a `T` or `noreturn`.
- **Return lifting.** In a function returning `?T`, `return v` with `v: T` returns `some(v)`. In a function returning `E!T`, `return v` with `v: T` returns the value, and `return e` with `e` of an error-set type returns the error. This is the only implicit wrapping in Arche, and it applies only to `return`.
- **Results are used.** An expression statement whose value is not `void` is an error, for error unions included. `_ = expr;` drops a value on purpose.

## 8. Statements

```arche
let a: u32 = 1;
var b = a;
b += 2;
_ = maybe_fails();

if b > 2 {
    b = 0;
} else if b == 2 {
    b = 1;
} else {
    b = 2;
}

var i: usize = 0;
while i < items.len() : (i += 1) {
    if skip(items[i]) {
        continue;
    }
    if done(items[i]) {
        break;
    }
}

defer sys.stdout.write("done\n");
```

- `if` and `while` take a `bool` condition without parentheses, and their bodies are always blocks.
- `while c : (step) {…}` runs `step` after every iteration, including one ended by `continue`.
- `break` and `continue` apply to the innermost `while`. There are no labels.
- `defer stmt;` or `defer {…}` runs when the enclosing block exits, on every path: falling off the end, `return`, `break`, `continue`, or an error returned by `try`. Deferred code runs in reverse order of declaration and cannot itself `return`, `break`, `continue` or use `try`. On `return`, the returned value is computed before the deferred code runs.
- A block introduces a scope.

## 9. Patterns and `match`

```arche
match expr {
    .Int { value } => {
        total += value;
    }
    .Binary { op: kind, .. } => {
        record(kind);
    }
}

match byte {
    'a'..='z' | 'A'..='Z' | '_' => { lex_ident(); }
    '0'..='9' => { lex_number(); }
    _ => { lex_symbol(); }
}

match lookup(name) {
    some(id) => { use_id(id); }
    none => { report_missing(); }
}
```

| Pattern | Matches |
| --- | --- |
| `_` | anything, binds nothing |
| `name` | anything, binds it to `name` |
| `42`, `-1`, `'a'` | an integer or character |
| `lo..=hi` | an integer in the inclusive range |
| `true`, `false` | a `bool` |
| `.V` | a payload-free variant, or an error name |
| `.V { f, g: x, h: _, .. }` | a variant; binds `f` as `f` and `g` as `x`, ignores `h`; `..` ignores the remaining fields |
| `some(p)`, `none` | an optional |
| `ok(p)`, `err(p)` | an error union |
| `p1 \| p2` | either pattern; alternatives cannot bind names |

- Arms are tried in order. An arm body is always a block; a comma after it is optional.
- A variant pattern names every field of the variant, unless it ends with `..`.
- `match` must be exhaustive.
- **Bindings.** Matching a value binds copies of copyable fields and moves move-only fields out of the scrutinee, which must then be a local or a temporary. Matching `&x` or `&mut x` binds views of the fields instead and moves nothing.

## 10. Functions

- `fn name(p: T, …) -> R {…}`. There are no default arguments, no variadic functions and no overloading: a name refers to one function.
- A function that returns a value must end every path with `return` or with a `noreturn` expression. An `if` ends a path when it has an `else` and every branch ends it; a `match` does when every arm does; `while true` does when its body has no `break` of its own.
- Recursion is allowed.
- **Entry point.** The root module is the file the compiler is given, and its directory is the package root (§3). It declares `fn main(sys: &mut std.System) -> !void`. The capabilities in `sys` are all the program can reach (§11).
- **Exit.** However the process exits, `sys.stdout` and `sys.stderr` are flushed first. If `main` returns an error, `error: Name` is written to stderr and the process exits with status 1. A trap or `panic` writes `panic: <message>` to stderr, then `  at <file>:<line>:<column>` when the cause is in the source, and the process exits with status 101. `<file>` is relative to the package root; lines and columns count from 1, and columns count bytes.

## 11. Library available to Arche-0

stage0 implements exactly this surface natively. The full library provides the same signatures.

**Built-ins** (no import):

```arche
fn panic(message: str) -> noreturn
fn assert(condition: bool)
fn unreachable() -> noreturn
```

**`str` methods:**

```arche
fn len(self: str) -> usize
fn eq(self: str, other: str) -> bool
fn starts_with(self: str, prefix: str) -> bool
fn bytes(self: str) -> []u8                       // projection
```

**`alloc`:**

```arche
type Allocator                                    // copyable handle
type Symbol                                       // copyable key type

type List(T)                                      // move-only
    fn init(a: Allocator) -> List(T)
    fn push(self: &mut List(T), item: T)
    fn pop(self: &mut List(T)) -> ?T
    fn len(self: &List(T)) -> usize
    fn clear(self: &mut List(T))
    fn items(self: &List(T)) -> []T               // projection
    fn items_mut(self: &mut List(T)) -> []mut T   // projection

type Map(K, V)                                    // move-only, insertion-ordered
    fn init(a: Allocator) -> Map(K, V)
    fn put(self: &mut Map(K, V), key: K, value: V)
    fn get(self: &Map(K, V), key: K) -> ?V
    fn remove(self: &mut Map(K, V), key: K) -> ?V
    fn len(self: &Map(K, V)) -> usize

type String                                       // move-only
    fn init(a: Allocator) -> String
    fn push_str(self: &mut String, text: str)
    fn push_ascii(self: &mut String, byte: u8)     // traps on a byte above 0x7F
    fn view(self: &String) -> str                 // projection
    fn len(self: &String) -> usize

type Interner                                     // move-only
    fn init(a: Allocator) -> Interner
    fn intern(self: &mut Interner, text: str) -> Symbol
    fn text(self: &Interner, sym: Symbol) -> str  // projection
```

**`std`:**

```arche
type System = struct {
    pub fs: Fs,
    pub stdout: Output,
    pub stderr: Output,
    pub args: Args,
    pub alloc: alloc.Allocator,
}

type IoError = error { NotFound, AccessDenied, IsDirectory, InvalidUtf8, IoFailed }

type Fs                                           // capability, move-only
    fn read_file(self: &Fs, path: str, a: alloc.Allocator) -> IoError!alloc.String
    fn write_file(self: &Fs, path: str, data: str) -> IoError!void

type Output                                       // capability, move-only; flushed at exit
    fn write(self: &mut Output, text: str)
    fn write_byte(self: &mut Output, byte: u8)
    fn write_u64(self: &mut Output, n: u64)
    fn write_i64(self: &mut Output, n: i64)

type Args                                         // move-only
    fn len(self: &Args) -> usize
    fn get(self: &Args, i: usize) -> str          // projection; traps when out of range
```

`write_file` has two `str` parameters, so its calls are labeled: `sys.fs.write_file(path: out_path, data: text.view())`.

`sys.args.get(0)` is the program's own path. If an argument is not valid UTF-8, the program panics before `main` starts.

## 12. What stage0 may skip

Valid programs behave the same with or without these checks. stage0 may accept a program without performing them; the full compiler rejects programs that break them.

| Check | Section |
| --- | --- |
| No shadowing | 6.3 |
| Visibility (`pub`) | 4 |
| Required and correct labels | 7.5 |
| Results are used | 7.8 |
| View and move rules | 5.4, 5.5 |
| Integer literals need context | 5.6 |
| Operators mixed without parentheses | 7.1 |
| Inferred error sets only on non-`pub` functions | 5.2 |
| Exhaustive `match` (stage0 traps at run time instead) | 9 |
| Compiling `test` blocks | 4 |

Run-time checks stage0 **must** keep: array and slice bounds, `as` range, division and remainder by zero, shift width, `panic`, `assert`, `unreachable`, and the trap for a `match` that matches nothing. It **may** omit overflow checks on `+`, `-` and `*`; a valid stage1 never overflows.

## 13. Left out of Arche-0

| Feature | Why the compiler does not need it |
| --- | --- |
| User generics, `comptime`, `where` | the built-in containers cover it; handles replace generic trees |
| Interfaces, `dyn` | `match` on enums is the compiler's dispatch |
| Closures | an explicit state struct and a function |
| `if` and `match` as expressions | statements suffice and keep stage0 small |
| `for` loops | `while` with a step clause |
| Floating point | the compiler does no floating-point math |
| Raw pointers, `unsafe`, `asm` | containers are built in; the compiler calls no foreign code |
| Regions | the compiler allocates from one arena for its whole run |
| Concurrency | the bootstrap compiler is single-threaded |
| Contracts (`requires` / `ensures`) | to be added as erasable checks once stage1 works |
| Labeled `break` / `continue` | rare; a helper function does the same |

## 14. Grammar

`{ x }` is zero or more, `[ x ]` is optional, `|` separates choices, and terminals are quoted. `IDENT`, `INT`, `CHAR`, `STRING` and `DOC` are tokens. The grammar is LL(1) except for argument labels, which look one token further (`IDENT` followed by `:`). The parser collects `DOC` tokens and attaches them to whatever follows; they never decide between alternatives.

```ebnf
file        = { import } { item } ;
import      = "import" path [ "as" IDENT ] ";" ;
path        = IDENT { "." IDENT } ;

item        = { DOC } ( [ "pub" ] decl | test_decl ) ;
decl        = const_decl | type_decl | fn_decl ;
const_decl  = "const" IDENT ":" type "=" expr ";" ;
type_decl   = "type" IDENT "=" type_body ;
type_body   = "struct" "{" { member } "}"
            | "enum" "{" variant { "," variant } [ "," ] { member } "}"
            | "error" "{" IDENT { "," IDENT } [ "," ] "}"
            | type ";" ;
member      = { DOC } [ "pub" ] ( field | fn_decl ) ;
field       = IDENT ":" type [ "," ] ;
variant     = { DOC } IDENT [ "{" fields "}" ] ;
fields      = IDENT ":" type { "," IDENT ":" type } [ "," ] ;
fn_decl     = "fn" IDENT "(" [ params ] ")" [ "->" type ] block ;
params      = param { "," param } [ "," ] ;
param       = IDENT ":" type ;
test_decl   = "test" STRING block ;

type        = "?" type
            | "!" type
            | "&" [ "mut" ] type
            | "[" ( "]" [ "mut" ] type | expr "]" type )
            | named [ "!" type ] ;
named       = path [ "(" type { "," type } [ "," ] ")" ] ;

block       = "{" { stmt } "}" ;
stmt        = ( "let" | "var" ) IDENT [ ":" type ] "=" expr ";"
            | "if" if_rest
            | "while" expr [ ":" "(" assign ")" ] block
            | "match" expr "{" { arm } "}"
            | "return" [ expr ] ";"
            | "break" ";"
            | "continue" ";"
            | "defer" ( block | simple ";" )
            | block
            | simple ";" ;
if_rest     = expr block [ "else" ( "if" if_rest | block ) ] ;
simple      = "_" "=" expr | assign ;
assign      = expr [ assign_op expr ] ;
assign_op   = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "+%=" | "-%=" | "*%="
            | "&=" | "|=" | "^=" | "<<=" | ">>=" ;
arm         = pattern { "|" pattern } "=>" block [ "," ] ;

pattern     = "_" | IDENT | literal [ "..=" literal ]
            | "true" | "false" | "none"
            | ( "some" | "ok" | "err" ) "(" pattern ")"
            | "." IDENT [ "{" field_pats "}" ] ;
field_pats  = ".." | field_pat { "," field_pat } [ "," [ ".." ] ] ;
field_pat   = IDENT [ ":" ( IDENT | "_" ) ] ;
literal     = [ "-" ] INT | CHAR ;

expr        = or_expr [ ( "orelse" | "catch" ) expr ] ;
or_expr     = and_expr { "or" and_expr } ;
and_expr    = cmp_expr { "and" cmp_expr } ;
cmp_expr    = bit_expr [ cmp_op bit_expr ] ;
bit_expr    = shift_expr { ( "&" | "^" | "|" ) shift_expr } ;
shift_expr  = add_expr { ( "<<" | ">>" ) add_expr } ;
add_expr    = mul_expr { ( "+" | "-" | "+%" | "-%" ) mul_expr } ;
mul_expr    = cast_expr { ( "*" | "/" | "%" | "*%" ) cast_expr } ;
cast_expr   = prefix { ( "as" | "as%" ) type } ;
prefix      = ( "-" | "~" | "not" | "try" | "&" [ "mut" ] ) prefix | postfix ;
postfix     = primary { call | "." IDENT | ".*" | index | struct_lit } ;
call        = "(" [ arg { "," arg } [ "," ] ] ")" ;
arg         = [ IDENT ":" ] expr ;
index       = "[" expr [ ".." expr ] "]" ;
struct_lit  = "{" [ IDENT ":" expr { "," IDENT ":" expr } [ "," ] ] "}" ;
primary     = INT | CHAR | STRING | "true" | "false" | "none"
            | "some" "(" expr ")"
            | IDENT
            | "(" expr ")"
            | "[" [ expr { "," expr } [ "," ] ] "]" ;
cmp_op      = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
```

Constraints the grammar does not express: a struct literal follows only a path, and it is not the condition of `if` or `while` or the scrutinee of `match` unless it is in parentheses (§7.4); the mixing rules of §7.1; fields come before methods (§4); `.*` applies only to references.

## 15. A complete program

```arche
import std;

fn main(sys: &mut std.System) -> !void {
    if sys.args.len() != 2 {
        sys.stderr.write("usage: lines <file>\n");
        return;
    }
    let path = sys.args.get(1);
    let text = try sys.fs.read_file(path, sys.alloc);
    sys.stdout.write_u64(count_lines(text.view()));
    sys.stdout.write("\n");
}

fn count_lines(text: str) -> u64 {
    var n: u64 = 0;
    var i: usize = 0;
    while i < text.len() : (i += 1) {
        if text[i] == '\n' {
            n += 1;
        }
    }
    return n;
}
```

And the handle style the compiler uses for its trees:

```arche
import alloc;

type NodeId = struct { index: u32 }

type Node = enum {
    Int { value: u64 },
    Add { lhs: NodeId, rhs: NodeId },
}

type Ast = struct {
    nodes: alloc.List(Node),

    fn add(self: &mut Ast, node: Node) -> NodeId {
        let id = NodeId { index: self.nodes.len() as u32 };
        self.nodes.push(node);
        return id;
    }

    fn eval(self: &Ast, id: NodeId) -> u64 {
        match self.nodes[id.index as usize] {
            .Int { value } => {
                return value;
            }
            .Add { lhs, rhs } => {
                return self.eval(lhs) + self.eval(rhs);
            }
        }
    }
}
```

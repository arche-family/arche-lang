# Arche

**Status:** draft 0.1 · 2026-11-04

This is the specification of Arche. It is built on [Arche-0](arche-0.md), the bootstrap subset: **every rule of Arche-0 is a rule of Arche, unchanged**, and every valid Arche-0 program is a valid Arche program with the same meaning. This document adds what Arche-0 leaves out (Arche-0 §13), chapter by chapter, and ends with the whole grammar.

Where this document and Arche-0 seem to disagree, this document only ever adds: a construct Arche-0 rejects may be valid here, but nothing valid in Arche-0 changes meaning. The section numbers of Arche-0 are cited as A§n.

| Chapter | Adds |
| --- | --- |
| 1 | Packages, editions and capabilities in the manifest |
| 2 | Floating point |
| 3 | `if` and `match` as expressions, `for`, labeled loops |
| 4 | Generics: `comptime` parameters and generic declarations |
| 5 | Interfaces, `where`, `dyn` |
| 6 | Closures |
| 7 | Regions |
| 8 | Contracts and `nopanic` |
| 9 | Concurrency: scopes, tasks, channels, `Shared(T)` |
| 10 | `unsafe`, raw pointers and the low level |
| 11 | The memory model, complete |
| 12 | Grammar |

The criterion of Arche-0 still decides every rule: you can tell what code does from the file it is in.

## 1. Packages, editions and capabilities

### 1.1 Packages

A package is a directory with a manifest, `package.arche`, at its root. The manifest is an Arche file that declares one constant of type `std.build.Package`:

```arche
import std.build;

const PACKAGE: build.Package = build.Package {
    name: "webd",
    version: "0.3.1",
    edition: 2027,
    needs: [build.Capability.Net, build.Capability.Fs, build.Capability.Clock],
    dependencies: [
        build.dep("http", "1.2.0", "sha256:9f2c…"),
    ],
};
```

- A module path names a file of the package (A§3). A dependency's modules are reached through the dependency's name: `import http.server;`.
- The roots `core`, `alloc` and `std` are the standard library; a package cannot use those names.

### 1.2 Editions

Every package declares an `edition`, a year. An edition may change syntax or rules; it never changes the meaning of code written for an older one, because a package is always compiled with the rules of its own edition, and packages of different editions link together. `arche fix --edition N` rewrites a package from its edition to edition N, and the result is checked to mean the same.

### 1.3 Capabilities

The capability types of `std` are `Fs`, `Net`, `Clock`, `Env`, `Proc` and `Rand`. A program reaches them only through `std.System` (A§10), and passes them on as parameters.

- **`needs`** in the manifest lists the capability types the package uses. A module that names a capability type its package does not list is an error (E0801). A dependency therefore states, in one line its users can read, everything it can reach.
- A capability can be **narrowed**: `sys.fs.within("data/")` is an `Fs` that reaches only that directory; `sys.net.connect_only("db.local:5432")` is a `Net` that can open only that connection. Narrowing never widens.

## 2. Floating point

### 2.1 Types and literals

`f32` and `f64` are IEEE 754 binary32 and binary64. A float literal has a decimal point or an exponent, `1.5`, `2.0e-3`, `1e9`, and takes its type from context like an integer literal (A§5.6); without context it is an error.

### 2.2 Arithmetic

- `+ - * /` round to nearest, ties to even. They do not trap: overflow gives an infinity, an invalid operation a NaN. `%` is not defined on floats; `math.rem` is.
- Comparisons follow IEEE 754: every comparison with a NaN is false, except `!=`, which is true. Floats are not key types (A§5.5) and do not implement `Eq`.
- There is no fused multiply-add unless written: `math.fma(a, b, c)`. The compiler never contracts or reorders float operations, so results are the same on every target.

### 2.3 Conversions

- `x as f64` from an integer rounds to nearest. `x as i32` from a float truncates toward zero and traps when the result does not fit or `x` is a NaN. `x as% i32` saturates instead, and gives 0 for a NaN.
- `x as f32` from an `f64` rounds to nearest; `x as f64` from an `f32` is exact.

### 2.4 Text

`std.fmt` writes a float as the shortest decimal that reads back to the same value, so output is the same on every target.

## 3. Expressions and loops

### 3.1 `if` and `match` as expressions

`if` and `match` may be expressions. Each branch is a block whose last item is an expression without a following `;`, the branch's value:

```arche
let kind = if n < 0 { "negative" } else { "other" };
let width: u32 = match shape {
    .Rect { w, .. } => { w }
    .Circle { radius } => { 2 * radius }
};
```

- An `if` expression has an `else`; a `match` expression is exhaustive. All branches have one type, or are `noreturn`.
- A block used as a statement ends without a value, as in Arche-0. A value-ending block appears only as a branch of an `if` or `match` expression.

### 3.2 `for`

```arche
for i in 0..n { … }            // 0, 1, …, n - 1
for i in 0..=n { … }           // 0, …, n
for item in list.items() { … } // item: &T, a view of each element
for (i, item) in list.items().enumerate() { … }
```

- `for x in e` evaluates `e` once. If `e` has a method `iter`, the loop uses `e.iter()`; then the value must implement `core.Iter(T)` (§5.1): `fn next(self: &mut Self) -> ?T`. Each iteration binds `x` to the next item, until `next` gives `none`.
- `for (a, b) in e` destructures an item of a struct type with two fields; it is the only destructuring in `for`.
- Iterating a slice `[]T` gives views `&T`; iterating `[]mut T` gives `&mut T`. While the loop runs, the slice is live (A§5.4).
- A range `a..b` is a value of `core.Range(T)` for an integer type `T`; `a..=b` of `core.RangeInclusive(T)`. Both are empty when `a > b`.

### 3.3 Labeled loops

A loop may have a label, `outer: while … { … }` or `outer: for … { … }`, and `break outer;` or `continue outer;` then applies to it. Labels are not names: they cannot be passed or stored, and an inner label cannot repeat an outer one.

## 4. Generics

Arche has one generic mechanism: parameters known at compile time. There are no type parameter lists separate from ordinary parameters, and no inference of type arguments: the reader of a call sees every argument.

### 4.1 `comptime` parameters

```arche
fn max(comptime T: type, a: T, b: T) -> T where T: core.Ord {
    if a.less(b) {
        return b;
    }
    return a;
}

let m = max(u32, a: x, b: y);
```

- A parameter marked `comptime` takes a value known at compile time: a type (`T: type`), an integer, a `bool` or a `str`. Its argument must be a constant expression (A§4) or another `comptime` parameter.
- A function with `comptime` parameters is instantiated once for each distinct list of `comptime` arguments; each instance is checked and compiled as an ordinary function.
- The label rule (A§7.5) compares declared types as written, so `a: T, b: T` need labels and `key: K, value: V` do not.

### 4.2 Generic types

A type declaration may take `comptime` parameters:

```arche
pub type Pair(comptime A: type, comptime B: type) = struct {
    pub first: A,
    pub second: B,
}

pub type Grid(comptime T: type, comptime W: usize, comptime H: usize) = struct {
    cells: [W * H]T,
}

let p = Pair(u32, str) { first: 1, second: "x" };   // an error: str is a view (A§5.4)
let g: Grid(u8, 4, 4) = Grid(u8, 4, 4).init();
```

- `alloc.List(T)`, `alloc.Map(K, V)` and the other library types of Arche-0 are generic types in this sense.
- Two instances are the same type exactly when their arguments are equal: `Pair(u32, u8)` is one type wherever it is written.

### 4.3 `comptime` blocks and the interpreter

- `comptime { … }` and `comptime` expressions run at compile time, in an interpreter over the IR. They may call any function whose body uses no capability and no `unsafe`; they cannot do I/O.
- The interpreter counts steps. A compilation that runs more than 10⁸ steps at compile time, or recurses deeper than 1000 calls, is an error (E0840); `arche build --comptime-profile` shows where the steps went.
- Types are values of type `type` at compile time. `core.type_info(T)` describes a type (its fields, variants and their types), so generated code, such as JSON encoding, is written in Arche, not in a macro language.

## 5. Interfaces

### 5.1 Declaring and implementing

An interface is a set of method signatures:

```arche
pub type Hash = interface {
    fn hash(self: &Self, h: &mut Hasher);
}

pub type Iter(comptime T: type) = interface {
    fn next(self: &mut Self) -> ?T;
}

impl Hash for Point {
    fn hash(self: &Point, h: &mut Hasher) {
        h.write_i32(self.x);
        h.write_i32(self.y);
    }
}
```

- `Self` is the implementing type.
- **`impl I for T`** declares that `T` implements `I`, with a body of the interface's methods. It may appear only in the module that declares `T` or the one that declares `I`, so the implementations of a type are found in two places at most, both named in its code. A type implements an interface only through an `impl`; having the methods is not enough.
- The methods of an `impl` are methods of `T`: `p.hash(&mut h)`. If two interfaces a type implements have a method of one name, calls must name the interface: `Hash.hash(&p, &mut h)`.
- The library declares `core.Eq`, `core.Ord`, `core.Hash`, `core.Iter(T)`, `core.Format` and `alloc.Allocator`; `==` on a struct is `Eq.eq` once the struct implements `Eq`, and A§5.5's field-by-field `==` remains for structs without it.

### 5.2 `where`

`where` constrains `comptime` type parameters:

```arche
fn sort(comptime T: type, items: []mut T) where T: core.Ord { … }
pub type Set(comptime T: type) = struct where T: core.Hash + core.Eq { … }
```

Inside, a value of type `T` has exactly the methods of its constraints. A call checks the constraints against the arguments, and an error names the argument and the missing interface (E0830).

### 5.3 `dyn`

`&dyn I` and `&mut dyn I` are views of a value of any type that implements `I`: a pointer to the value and one to the table of `I`'s methods for its type. Calls through them are dynamic.

- `&x` converts to `&dyn I` where a `&dyn I` is expected, when `x`'s type implements `I`; no other conversion is implicit.
- An interface is usable with `dyn` when its methods take `self` by reference and have no `comptime` parameters.
- `dyn I` values are views (A§5.4): they are passed and held in locals, not stored.

## 6. Closures

A closure is a function value written in place. Its captures are listed:

```arche
var total: u64 = 0;
let add = fn [&mut total](x: u64) { total += x; };
items.each(add);

let scale = fn [factor](x: u32) -> u32 { return x * factor; };
```

- The list in brackets after `fn` names every binding the body uses from outside: `x` captures a copy (a copyable value) or moves `x` (a move-only one); `&x` and `&mut x` capture views. The body can use no other outside binding; there is no implicit capture.
- A closure's type is unnamed and unique. It implements `core.Fn(P, R)`, where `P` is its parameter list and `R` its result; a closure that captures by `&mut` implements `core.FnMut(P, R)`. They are taken as `comptime F: type where F: core.Fn(…)` or as `&dyn core.Fn(…)`.
- A closure that captures a view is itself a view: it cannot be stored, returned, or outlive the bindings it views (A§5.4). A closure that captures only by value is an ordinary value and may be stored and moved.
- Closures do not capture `return`, `break` or `continue`: those apply to the closure's own body.

## 7. Regions

A region is an arena that lives for one block and is freed in one step when the block ends. A web server runs each request in its own region (docs/plan.md).

```arche
region r {
    var body = alloc.String.init(r);
    body.push_str(request.path());
    let reply = render(&body, r);
    try conn.write(reply.view());
}   // everything allocated from r is freed here
```

- `region r { … }` binds `r`, an `alloc.Allocator`, for the block.
- **Region-bound values.** A value is bound to `r` when it was made with `r`, or holds a value bound to `r`. The compiler tracks this through bindings, fields, returns and calls: a function that takes an allocator and returns a container is assumed to bind the result to that allocator.
- **Escape.** A value bound to `r` cannot leave the block: it cannot be assigned to a binding declared outside it, be returned from it, be moved into a container that is not bound to `r`, or be sent to another task (E0901). Copying out what is copyable is allowed: `let n = body.len();`.
- Regions nest. A value of an inner region may hold values of an outer one, never the other way around: the inner region ends first.
- Leaving the block on any path, by `return`, `break`, `try` or a panic, frees the region.

## 8. Contracts and `nopanic`

### 8.1 `requires` and `ensures`

```arche
fn split(s: str, at: usize) -> Halves
    requires at <= s.len()
    ensures result.left.len() == at
{
    …
}
```

- `requires` conditions are checked at every call, before the body; `ensures` conditions after it, where `result` names the result. A failed condition traps with `contract failed: <condition>` and the location of the call.
- Debug builds check every condition. Release builds remove those the compiler proves always true, and keep the rest.
- A condition is a `bool` expression over the parameters (and `result`) that calls only functions without capabilities, side effects or `unsafe`.

### 8.2 `nopanic`

`nopanic fn` declares that the function never panics or traps. The compiler checks it: the body may call only `nopanic` functions, and every operation that could trap (checked arithmetic, indexing, `as`) must be one the compiler proves safe, or be written in a form that cannot trap (`+%`, `.get(i)`, `as%`) (E0910). Kernel interrupt handlers and the allocator are `nopanic`.

### 8.3 Error return traces

When `try` passes an error up, the location is recorded in a per-task trace of fixed size. `main` returning an error prints the trace after `error: <name>` in debug builds. Recording costs nothing on the success path.

## 9. Concurrency

Arche has structured concurrency: a task never outlives the scope that started it, so a task can borrow from that scope.

```arche
std.task.scope(sys.proc, fn [&files, &mut results](s: &mut task.Scope) {
    for path in files.items() {
        s.spawn(fn [path, &results]() -> !void {
            …
        });
    }
});   // returns when every task has finished
```

- **Scopes.** `task.scope(proc, f)` runs `f` with a `Scope`; `s.spawn(g)` starts `g` as a task in it. `scope` returns when all of its tasks have finished. A task's panic is reported by `scope` as an error after the others finish.
- **What tasks share.** A task's closure captures by value (moving), or by `&` view of data that outlives the scope. A `&mut` view can be captured by at most one task, and not used by the scope's own code until the tasks end (A§5.4, applied to tasks).
- **`Shared(T)`** is the only way for tasks to change shared data: `let s = Shared(Counter).init(c, a);` and `s.lock(fn [](c: &mut Counter) { … })`. The view exists only inside the lock, so it is never held across another lock. `Shared(T)` is reference-counted; copying it with `.share()` is explicit.
- **Channels.** `task.Channel(T)` with `send(value)` (moving it) and `recv() -> ?T`; `none` once every sender has been dropped and the channel is empty. Channels are bounded; a full channel blocks the sender.
- **No async/await.** Tasks are scheduled by the runtime onto threads; blocking calls block only their task.
- **Determinism in tests.** `arche test` runs tasks under a scheduler driven by a seed it prints, so a failure repeats with the same seed.

## 10. The low level

### 10.1 `unsafe`

`unsafe { … }` marks code whose correctness the compiler cannot check. Only inside it:

- **Raw pointers** `*T` and `*mut T`: made from views with `&x as *T`, or from integers with `ptr.from_addr(T, n)`; read with `p.*`, offset with `p.add(n)`. Raw pointers are copyable values and may be stored anywhere; the rules of A§5.4 do not apply to them.
- Calls to `unsafe fn`, foreign functions (`extern "c" fn`) and the operations below.
- Undefined behaviour is possible only inside `unsafe`: outside it, every program either does what the specification says or traps.

Every `unsafe` block carries a comment that begins `// SAFETY:` and says why it is correct; a block without one is an error in a package that sets `strict_unsafe` (E0920).

### 10.2 Operations for kernels and drivers

These exist for the `freestanding` target (S16) and are all `unsafe`:

| Feature | Form |
| --- | --- |
| Inline assembly | `asm("mov %0, cr3", in(reg) addr)` with typed operands and clobbers |
| Naked functions | `naked fn`: no prologue or epilogue; the body is one `asm` block |
| Volatile access | `ptr.read_volatile(p)`, `ptr.write_volatile(p, v)` |
| Atomics | `core.atomic.U32` and friends, with an explicit `Ordering` on every operation: `Relaxed`, `Acquire`, `Release`, `AcqRel`, `SeqCst` |
| Barriers | `core.atomic.fence(ordering)` |
| Layout | `packed struct`, `align(N)` on a type or field, `extern struct` for C's layout |
| Sections | `section(".text.boot")` on a function or constant |
| Panic handler | `panic_handler: "kernel.panic.halt"` in the manifest names a `noreturn` function that a panic calls, for targets without `std` |
| Target features | `target_features: ["-sse", "-avx"]` in the manifest |

### 10.3 Targets

`std` exists for hosted targets (Linux and Windows) and Arche OS. The `freestanding` target has `core` and `alloc` only: no capabilities, and a panic handler supplied by the program.

## 11. The memory model

This chapter collects every rule of Arche's memory model in one place. Each has its own error code. There is no garbage collector, there are no lifetime annotations, and outside `unsafe` no program can read freed memory or race.

### 11.1 Values and moves (A§5.5)

| Rule | Code |
| --- | --- |
| A move-only value (a container, a capability, a `Shared(T)`, a closure that captures a move-only value, or anything holding one) is moved when it is passed, assigned, returned, stored, captured by value or matched by value; the source is unusable on every path that follows until it is reassigned | E0605 |
| Nothing is moved out of a place reached through a view; copy it, or move the whole value the view comes from | E0606 |
| Moving a field moves only that field; the rest of the struct stays usable, but the struct as a whole does not until the field is reassigned | E0605 |

### 11.2 Views (A§5.4)

| Rule | Code |
| --- | --- |
| A view (`&T`, `&mut T`, `[]T`, `[]mut T`, `str`, `&dyn I`, a closure that captures a view) is only ever the type of a parameter, a local, a result or the target of a reference: never a field, payload, element, or payload of `?` or `!` | E0601 |
| A function returns a view only with at most one view parameter; the result is a projection of it, or of static memory | E0602 |
| A view never outlives what it views: it is not returned, stored in an outer binding, captured by a task, or bound to a longer-lived region, when what it views does not live that long | E0603 |
| While a view of a place is live, the place is not written or viewed `&mut` through another path; while a `&mut` view is live, the place is not used through another path at all. The views given to one call are live during the call | E0604 |

Liveness: a view is live from where it is made to its last use, and through the rest of any loop that uses it. Paths are compared by their fields and elements; elements of one array, list or slice are one path.

### 11.3 Regions, tasks and closures

| Rule | Code |
| --- | --- |
| A value bound to a region does not leave the region's block (§7) | E0901 |
| A task captures a `&mut` view only if neither another task nor its scope uses that place while the task runs (§9) | E0902 |
| A closure that captures a view is a view; one that captures by value is a value, move-only when a capture is (§6) | E0601, E0605 |
| Tasks share changeable data only through `Shared(T)`, whose view exists only inside `lock` | E0902 |

### 11.4 `unsafe`

The rules above do not apply to raw pointers. Code that turns a raw pointer back into a view (`p.as_ref()`, `p.as_mut()`) promises, in its `// SAFETY:` comment, that the view obeys §11.2 for as long as it lives. A broken promise is undefined behaviour, and only `unsafe` code can make one.

## 12. Grammar

This grammar extends A§14. Rules with the same name replace those of Arche-0; the others are added. As in Arche-0 the grammar is LL(1), except where it says otherwise: an argument label (`IDENT ":"` in a call) and a loop label (`IDENT ":"` before `while` or `for`) look one token further.

```ebnf
file        = { import } { item } ;
item        = { DOC } ( [ "pub" ] decl | test_decl | impl_decl ) ;
decl        = const_decl | type_decl | fn_decl | extern_decl ;

type_decl   = "type" IDENT [ "(" cparams ")" ] [ where ] "=" type_body ;
type_body   = [ "packed" | "extern" ] "struct" [ align ] "{" { member } "}"
            | "enum" "{" variant { "," variant } [ "," ] { member } "}"
            | "error" "{" IDENT { "," IDENT } [ "," ] "}"
            | "interface" "{" { fn_sig ";" } "}"
            | type ";" ;
cparams     = cparam { "," cparam } [ "," ] ;
cparam      = "comptime" IDENT ":" type ;
align       = "align" "(" expr ")" ;
impl_decl   = "impl" named "for" type "{" { [ "pub" ] fn_decl } "}" ;

fn_decl     = fn_sig { contract } block ;
fn_sig      = { fn_mod } "fn" IDENT "(" [ params ] ")" [ "->" type ] [ where ] ;
fn_mod      = "nopanic" | "naked" | "unsafe" ;
params      = param { "," param } [ "," ] ;
param       = [ "comptime" ] IDENT ":" type ;
contract    = ( "requires" | "ensures" ) expr ;
where       = "where" bound { "," bound } ;
bound       = IDENT ":" named { "+" named } ;
extern_decl = "extern" STRING fn_sig ";" ;

type        = "?" type
            | "!" type
            | "&" [ "mut" ] ( "dyn" named | type )
            | "*" [ "mut" ] type
            | "[" ( "]" [ "mut" ] type | expr "]" type )
            | named [ "!" type ] ;
named       = path [ "(" targ { "," targ } [ "," ] ")" ] ;
targ        = type | "comptime" expr ;

block       = "{" { stmt } "}" ;
value_block = "{" { stmt } expr "}" ;
stmt        = ( "let" | "var" ) IDENT [ ":" type ] "=" expr ";"
            | "if" if_rest
            | [ IDENT ":" ] loop
            | "match" expr "{" { arm } "}"
            | "return" [ expr ] ";"
            | ( "break" | "continue" ) [ IDENT ] ";"
            | "defer" ( block | simple ";" )
            | "region" IDENT block
            | "unsafe" block
            | "comptime" block
            | block
            | simple ";" ;
loop        = "while" expr [ ":" "(" assign ")" ] block
            | "for" for_bind "in" expr block ;
for_bind    = IDENT | "(" IDENT "," IDENT ")" ;

expr        = or_expr [ ( "orelse" | "catch" ) expr ] ;
primary     = INT | FLOAT | CHAR | STRING | "true" | "false" | "none"
            | "some" "(" expr ")"
            | IDENT
            | "(" expr ")"
            | "[" [ expr { "," expr } [ "," ] ] "]"
            | "if" expr value_block "else" ( "if" if_expr_rest | value_block )
            | "match" expr "{" { pattern { "|" pattern } "=>" value_block [ "," ] } "}"
            | "fn" "[" [ capture { "," capture } [ "," ] ] "]" "(" [ params ] ")" [ "->" type ] block
            | "asm" "(" STRING { "," asm_operand } ")" ;
if_expr_rest = expr value_block "else" ( "if" if_expr_rest | value_block ) ;
capture     = [ "&" [ "mut" ] ] IDENT ;
asm_operand = ( "in" | "out" | "inout" ) "(" IDENT ")" expr | "clobber" "(" STRING ")" ;
```

The other rules of A§14 (`if_rest`, `simple`, `assign`, `arm`, `pattern`, the operator levels and `postfix`) are unchanged. `FLOAT` is a decimal literal with a `.` or an exponent (§2.1). `for`, `in`, `interface`, `dyn`, `comptime`, `region`, `requires`, `ensures`, `where`, `nopanic`, `naked`, `unsafe` and `asm`, reserved in Arche-0 (A§2.1), are keywords here.

`impl`, `extern`, `packed`, `align` and `Self` are **contextual**: they mean what this chapter says only where the grammar puts them (`impl` at the start of an item, `extern` before a string or `struct`, `packed` before `struct`, `align` after `struct` or in a field, `Self` inside an interface or an `impl`). Everywhere else they are ordinary identifiers, so no Arche-0 program that uses them as names changes meaning. Inside `asm(…)`, `in`, `out`, `inout` and `clobber` are contextual the same way.

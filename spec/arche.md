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

- The manifest is read, not run: its fields are `name` (required), `version`, `edition`, `needs`, `dependencies`, `strict_unsafe` (§10.1) and `panic_handler` (§10.3), each a literal of its kind; anything else is E0802. A `package.arche` of comments only still makes the directory a full-Arche package, with every capability and edition 2026.
- A package that builds programs lists them in `build.arche`, read the same way: `const BUILD: build.Build = build.Build { programs: [build.program("webd", "main.arche")] };`. `arche-driver build` builds them into `build/`, `arche-driver run` builds and runs one (the compiler stays in Arche-0, which cannot start processes, so the driver, in full Arche, drives it and clang). Each module is compiled on its own, to an object kept under the SHA-256 of its code, so a build after a change compiles only the modules that changed, and its output is byte for byte that of a clean build.
- A module path names a file of the package (A§3). A dependency's modules are reached through the dependency's name: with a dependency `webkit`, `import webkit;` is its file `webkit.arche` and `import webkit.server;` its `server.arche`. Inside a dependency, other paths name its own modules; in the root package, a path may begin with the package's own name. A dependency's modules follow its own manifest: its `needs`, `strict_unsafe` and `edition`.
- **Dependencies.** `dependencies` lists `build.dep(name, version)`, with an optional third argument pinning the archive's hash, `"sha256:…"`; `registry` names where they come from, a directory or `https://host:port/path/`, and `registry_key` the hex Ed25519 public key that signs its releases. A registry is content-addressed: a version's archive is kept under the SHA-256 of its bytes and never changes, and a release record that names that hash is signed by the key. `arche-driver pkg lock` checks every record's signature and every archive's hash, resolves the dependencies of dependencies (two versions of one package are an error), and writes `package.lock`, a line `name version sha256:hash` for each; `pkg vendor` unpacks the locked archives, their hashes checked again, into `vendor/<name>/`, where the compiler finds them; `pkg add` adds one and does both. An archive is the package's files in byte order of their paths, so the same files give the same archive, and the same sources and lockfile give the same program. `pkg publish` and `pkg keygen` make a registry, and `arche-registry` serves one over HTTPS.
- The roots `core`, `alloc`, `std`, `crypto`, `tls` and `http` are the standard library; a package cannot use those names. The library is full Arche, in the repository's `lib/`: `core` (`lib/core.arche` and `lib/core/`) needs no operating system and no allocator, and imports neither `alloc` nor `std`; `alloc` (`lib/alloc.arche`) has Arche-0's containers, written over `core.mem`, whose functions the compiler provides. A full-Arche package gets this `alloc`; an Arche-0 package keeps the compiler's own, which has the same types and signatures, and `arche --lib-alloc` gives it the library's. `std` (`lib/std.arche`, `lib/std/`) is the same for Linux: Arche-0's `System`, `Fs`, `Output`, `Args` and `IoError` with their signatures, and the capabilities below; `arche --lib-std` gives it to an Arche-0 package. `crypto` (`lib/crypto/`) is the cryptography library; only `crypto.random`, which seeds itself from `std.Rand`, needs `std`. Its code is written to be constant time, so no branch or memory index depends on a secret. `tls` (`lib/tls.arche`, `lib/tls/`) is TLS 1.3 over `crypto` and `std`, with X.509 certificates; `http` (`lib/http.arche`, `lib/http/`) is HTTP/1.1 and HTTP/2, a server and a client, over `std` and `tls`.

### 1.2 Editions

Every package declares an `edition`, a year. An edition may change syntax or rules; it never changes the meaning of code written for an older one, because a package is always compiled with the rules of its own edition, and packages of different editions link together. `arche fix --edition N` rewrites a package from its edition to edition N, and the result is checked to mean the same.

- The editions are 2026, the first, and 2027. A manifest without `edition`, or with no `PACKAGE` at all, is 2026; the library is 2026. Another year is E0803.
- **2027.** `ok` and `err` are names, not keywords, and the patterns of an error union are `.ok(p)` and `.err(p)`, written like the variants of an enum. `ok(p)` and `err(p)` as patterns are E0212, which `arche fix` corrects. In 2026 no name can be `ok` or `err`, so the move changes nothing else: `arche fix --edition 2027` puts the dot before each such pattern, sets `edition` in the manifest, and checks the package again.

### 1.3 Capabilities

The capability types of `std` are `Fs`, `Net`, `Clock`, `Env`, `Proc` and `Rand`. A program reaches them only through `std.System` (A§10), and passes them on as parameters.

- **`needs`** in the manifest lists the capability types the package uses: `[build.Capability.Fs, build.Capability.Net]`. A module of the package that names a capability type it does not list, `std.Net`, or uses that field of `std.System`, `sys.net`, is an error (E0801). A dependency therefore states, in one line its users can read, everything it can reach.
- **Tests.** `arche test <file> <out.ll>` builds the test blocks of the root module, not `main`, into a binary that runs each in a process of its own, so a panic fails that test and the others still run (`compiler/arche-test` builds and runs it). It always prints its seed first; `--seed n` repeats a run and `--filter text` runs the tests whose names contain `text`. In full Arche a test may take one parameter, `test "name" (t: &mut std.testing.Test) { … }` (anything else is E0929): `t.seed()` is the test's seed, derived from the run's; `t.rand()` a `core.rand.Seeded` generator from it; and `t.check(cases, property: &p)` calls the closure `p: fn(&mut core.rand.Seeded) -> bool` with a generator of its own for each case and panics, naming the case, when it is false. Tests run in parallel, as many at once as the machine has processors (`--jobs n` chooses); each test's output is kept and shown after its result line, in the tests' order, so a run reads the same however they interleave. The examples in doc comments, ` ```arche ` blocks, are tests too: `arche doc --tests <file> <module> <out>` writes them as a file of tests that imports the module, and an example that uses `sys` gets it from `t.system()`.
- `std.System` holds one of each: `fs`, `stdout`, `stderr`, `args`, `alloc`, `clock`, `env`, `proc`, `net`, `rand` and `stdin`, an `Input` with `read` and `ready`; like `stdout` it is no capability of `needs`. A capability's fields are private, so no code outside `std` can make one, and each is move-only.
- What they do: `Clock` reads the monotonic and the wall clock and sleeps; `Env` reads environment variables; `Proc` gives the process id, exits, runs a program and waits for it, and can make a write to a pipe nobody reads fail instead of ending the program (`ignore_broken_pipe`); `Net` connects and listens over TCP and IPv4; `Rand` gives the system's random bytes. `Fs` adds `exists`, `make_dir`, `make_private_dir` (one only this user can enter), `remove`, `rename`, `list` (sorted by byte order) and `temp_dir` (where files that need not outlive the program go, without a separator at its end) to Arche-0's three; those return `FsError`, since `IoError` keeps Arche-0's five names. Every host has them all (§10.3); `std.SYSTEM` is `"linux"` or `"windows"`, for a program that must tell the systems apart, such as a test runner.
- A capability can be **narrowed**: `sys.fs.within("data/")` is an `Fs` that reaches only that directory; `sys.net.connect_only("db.local:5432")` is a `Net` that can open only that connection. Narrowing never widens.

## 2. Floating point

### 2.1 Types and literals

`f32` and `f64` are IEEE 754 binary32 and binary64. A float literal has a decimal point or an exponent, `1.5`, `2.0e-3`, `1e9`, and takes its type from context like an integer literal (A§5.6); without context it is an error (E0405), and so is `as` on it, as on an integer literal. Its value is the float nearest to the decimal, ties to even; one too large for its type is E0404. An integer literal is not a float literal: `let x: f64 = 1;` is E0405.

### 2.2 Arithmetic

- `+ - * /` and unary `-` round to nearest, ties to even; both operands have one float type (E0402), and other operators are E0403. They do not trap: overflow gives an infinity, an invalid operation a NaN. `%` is not defined on floats; `math.rem` is.
- Comparisons follow IEEE 754: every comparison with a NaN is false, except `!=`, which is true. Floats are not key types (A§5.5) and do not implement `Eq`.
- There is no fused multiply-add unless written: `math.fma(a, b, c)`. The compiler never contracts or reorders float operations, so results are the same on every target.

### 2.3 Conversions

- `x as f64` from an integer rounds to nearest. `x as i32` from a float truncates toward zero and traps when the result does not fit or `x` is a NaN. `x as% i32` saturates instead, and gives 0 for a NaN.
- `x as f32` from an `f64` rounds to nearest; `x as f64` from an `f32` is exact.

### 2.4 Text

`std.fmt` writes a float as the shortest decimal that reads back to the same value, so output is the same on every target: plain digits when `1e-6 <= |x| < 1e21`, with `.0` when it is a whole number (`1.0`, `0.5`, `123456789.0`), and otherwise one digit, the rest after a `.`, and `e` with the exponent (`1e21`, `1.5e-7`, `5e-324`). Every output reads back as a float literal. NaN is `nan`, infinities `inf` and `-inf`, and zero keeps its sign, `-0.0`. Until `std.fmt` exists (sprint S18), `std.Output` has `write_f64` and `write_f32`.
- Floats are full Arche: in an Arche-0 package `f32`, `f64` and float literals do not exist.
- Constants are integers, `bool` or `str` (A§4); a float constant is E0705 for now, and float code does not run at compile time.

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

- An `if` expression has an `else`, and each branch ends with a value or never ends (E0512); a `match` expression is exhaustive (E0509). All branches have one type, or are `noreturn` (E0401). An `if` or `match` that ends a branch is that branch's value.
- A block used as a statement ends without a value, as in Arche-0 (E0201). A value-ending block appears only as a branch of an `if` or `match` expression.
- A view chosen by an `if` or `match` views static memory or one place in every branch (E0607): `if c { "a" } else { "b" }` is fine, `if c { &x } else { &y }` is not.

### 3.2 `for`

```arche
for i in 0..n { … }            // 0, 1, …, n - 1
for i in 0..=n { … }           // 0, …, n
for i: u8 in 0..10 { … }       // two literals: the name gives the type
for item in list.items() { … } // item: &T, a view of each element
for (i, item) in list.items() { … } // i: usize, the index
for entry in iterator { … }    // until iterator.next() gives none
```

- `for x in e` evaluates `e` once. If `e` has a method `iter`, the loop uses `e.iter()`; then the value must implement `core.Iter(T)` (§5.1): `fn next(self: &mut Self) -> ?T`. Each iteration binds `x` to the next item, until `next` gives `none`. Without `iter`, the loop owns `e`: it is moved, or copied, into the loop. Until the library declares `core.Iter` (sprint S15), a method `next` of that signature is enough.
- `for (a, b) in e` destructures an item of a struct type with two fields; it is the only destructuring in `for`. Over a slice, `for (i, x) in s` binds the index `i: usize` and the view `x`.
- Iterating a slice `[]T` gives views `&T`; iterating `[]mut T` gives `&mut T`. While the loop runs, the slice is live (A§5.4): the place it views cannot change through another path.
- A range `a..b` runs over an integer type `T`; `a..=b` includes `b` and stops there, so `0..=255` over `u8` ends without overflow. Both are empty when `a > b`. Ranges appear only in `for`. The bounds take their type as operands of `+` do (A§7.3); when both are literals, the name takes a type, `for i: u32 in 0..10`, and only a range loop's name does.
- The names a `for` binds cannot be assigned. A `for` loop over a range can run at compile time (§4.3).
- An item that cannot be iterated is E0810; a range or destructuring that does not fit is E0811.

### 3.3 Array repetition

`[v; n]` is an array of `n` copies of `v`, `n` a constant: `var buf: [4096]u8 = [0; 4096];`. The element type comes from context, or from `v`; a move-only `v` cannot be copied (E0401).

### 3.4 Labeled loops

A loop may have a label, `outer: while … { … }` or `outer: for … { … }`, and `break outer;` or `continue outer;` then applies to it. Labels are not names: they cannot be passed or stored, and an inner label cannot repeat an outer one. A label that no enclosing loop has, or one that repeats, is E0511.

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

- A parameter marked `comptime` takes a value known at compile time: a type (`T: type`), an integer, a `bool` or a `str`. Its argument must be a constant expression (A§4) or another `comptime` parameter. `comptime` parameters come before every other parameter, so a call's run-time arguments follow its compile-time ones.
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

- `Self` is the implementing type. It means that only inside an interface and an `impl`; elsewhere it is an ordinary name (§12).
- An interface's methods are signatures that end with `;`. Only they have no body. An interface is not a type of values: `fn f(s: Shape)` is an error (E0825), and a view of a value of any type that implements it is `&dyn Shape` (§5.3).
- **`impl I for T`** declares that `T` implements `I`, with a body of the interface's methods. It may appear only in the module that declares `T` or the one that declares `I`, so the implementations of a type are found in two places at most, both named in its code. A type implements an interface only through an `impl`; having the methods is not enough.
- An `impl` has each of the interface's methods, with the same parameters, parameter names and result, `Self` replaced by `T`, and no other methods (E0822). A type implements an interface once (E0823). `T` is a struct or enum type of the package (E0820); the module rule above is E0821.
- The methods of an `impl` are methods of `T`: `p.hash(&mut h)`. A type's own method and an `impl`'s cannot share a name (E0822). If two interfaces a type implements have a method of one name, calls must name the interface (E0824): `Hash.hash(&p, &mut h)`. `I.name(x, …)` calls the method of the `impl` of `I` for the type of `x`, or of `x.*` when `x` is a view, and is an error when there is none (E0832).
- The library declares `core.Eq`, `core.Ord`, `core.Hash`, `core.Iter(T)`, `core.Format` and `alloc.Allocator`; `==` on a struct is `Eq.eq` once the struct implements `Eq`, and A§5.5's field-by-field `==` remains for structs without it.

### 5.2 `where`

`where` constrains `comptime` type parameters:

```arche
fn sort(comptime T: type, items: []mut T) where T: core.Ord { … }
pub type Set(comptime T: type) = struct where T: core.Hash + core.Eq { … }
```

Inside, a value of type `T` has exactly the methods of its constraints. A call checks the constraints against the arguments, and an error names the argument and the missing interface (E0830); a type with a `where` checks them where an instance is named. A `where` names comptime type parameters of its declaration only (E0830).

### 5.3 `dyn`

`&dyn I` and `&mut dyn I` are views of a value of any type that implements `I`: a pointer to the value and one to the table of `I`'s methods for its type. Calls through them are dynamic.

- `&x` converts to `&dyn I` where a `&dyn I` is expected, when `x`'s type implements `I`; so does a closure written as an argument where a `&dyn fn(P) -> R` is expected, as a view of it for the call (§6). No other conversion is implicit.
- An interface is usable with `dyn` when its methods take `self` by reference and have no `comptime` parameters.
- `dyn I` values are views (A§5.4): they are passed and held in locals, not stored (E0601). A `&dyn I` made from `&x` views `x`, so while it is live `x` obeys A§5.4 rule 4.
- `&mut dyn I` converts to `&dyn I`, as `&mut T` to `&T`. A method that takes `&mut self` is called only through a `&mut dyn I`.
- `dyn` of an interface that is not usable with it is E0831; converting a view of a type that does not implement `I` is E0832.

## 6. Closures

A closure is a function value written in place. Its captures are listed:

```arche
var total: u64 = 0;
let add = fn [&mut total](x: u64) { total += x; };
items.each(add);

let scale = fn [factor](x: u32) -> u32 { return x * factor; };
```

- The list in brackets after `fn` names every binding the body uses from outside: `x` captures a copy (a copyable value) or moves `x` (a move-only one); `&x` and `&mut x` capture views, and `&mut x` needs a `var`. The body can use no other outside binding (E0850); there is no implicit capture. A capture names a local binding once (E0851); constants and functions are used without capturing.
- Inside the body a capture is used by its name, as the value or as the place it views: `total += x` changes the captured `total`. A copied or moved capture cannot be assigned.
- A closure's type is unnamed and unique. It implements the interface `fn(P) -> R`, where `P` is its parameter types and `R` its result, written as a type: `&dyn fn(u32) -> u32` is a view of any closure of that signature, and `where F: fn(u32) -> u32` a bound. `fn(P) -> R` is not a type of values (E0825). A closure is called like a function, `scale(7)`, and so is a `&dyn fn` view. Taking one as `comptime F: type` needs a way to name its type, which comes with `core` (sprint S15).
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

- `region r { … }` binds `r`, an `alloc.Allocator`, for the block. Containers made with `r` grow inside the region's arena; the arena is unmapped when the block ends.
- **Region-bound values.** A value is bound to `r` when it was made with `r`, or holds a value bound to `r`. The compiler tracks this through bindings, fields, returns and calls: a function that takes an allocator and returns a container is assumed to bind the result to that allocator.
- **Escape.** A value bound to `r` cannot leave the block: it cannot be assigned to a binding declared outside it, be returned from it, be moved into a container that is not bound to `r` (a method call on a container that takes it by value), or be sent to another task (E0901). `r` itself is bound to `r`. Copying out what is copyable is allowed: `let n = body.len();`.
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

- `requires` conditions are checked at every call, before the body; `ensures` conditions after it, at every return, where `result` names the result (a parameter cannot be named `result` then, E0305). A failed condition traps with `contract failed: <condition>` and the location of the condition. The checks live in the function itself, so every way of calling it, directly, through `&dyn` or as an instance, checks them.
- Debug builds check every condition. Release builds (`arche --release`) remove those the compiler proves always true, and keep the rest: a condition is a branch to a trap, and the optimizer drops the branch when constants and the checks already made decide it.
- A condition is a `bool` expression over the parameters (and `result`) (E0401) that calls only functions without capabilities, side effects or `unsafe`; the compiler does not check that last part yet.

### 8.2 `nopanic`

`nopanic fn` declares that the function never panics or traps. The compiler checks it: the body may call only `nopanic` functions, and every operation that could trap (checked arithmetic, indexing, `as`) must be one the compiler proves safe, or be written in a form that cannot trap (`+%`, `.get(i)`, `as%`) (E0910). Kernel interrupt handlers and the allocator are `nopanic`.

- The compiler proves safe: arithmetic on literals alone, division and remainder by a nonzero literal, a shift by a literal below the width, an array indexed by a literal inside it, an integer `as` that widens, and float arithmetic, which never traps. It knows nothing more yet; a later proof engine may accept more, never less.
- Calls through a closure or a `&dyn` view are not `nopanic`. Of the built-in methods, those that cannot fail are allowed: `len`, `items`, `items_mut`, `get`, `eq`, `starts_with`, `bytes`, `view`; methods that allocate, such as `push`, can fail and are not.

### 8.3 Error return traces

When `try` passes an error up, the location is recorded in a per-task trace of fixed size. `main` returning an error prints the trace after `error: <name>` in debug builds. Recording costs nothing on the success path.

- `return E.Name` starts a new trace at the error's location; each `try` that passes it on adds its own. The trace keeps the first 32 entries and prints them as A§13 prints a panic's location, one per line, from where the error was made to `main`:

```
error: Missing
  at main.arche:16:16
  at main.arche:22:13
```

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

- **Scopes.** `task.scope(&sys.proc, f)` runs `f` with a `Scope`; `s.spawn(g)` starts `g` as a task in it. `scope` returns when all of its tasks have finished. A task's panic ends that task, and `scope` reports it as `TaskError.Panicked` after the others finish; a task or `f` that returns an error makes it `TaskError.Failed`. Scopes nest: a task may open one of its own.
- **Spawning.** The closure given to `spawn` is written there, as its argument (E0903): the task copies it and owns its captures, so nothing else may still hold it. It is a `fn() -> !void`.
- **What tasks share.** A task's closure captures by value (moving), or by `&` view of data that outlives the scope: what the scope's closure itself captured, not its locals (E0603). A `&mut` view can be captured by at most one task, and is not used by the scope's own code until the tasks end (E0902; A§5.4, applied to tasks). Many tasks may capture one `&` view.
- **`Shared(T)`** is the only way for tasks to change shared data: `let s = task.Shared(Counter).init(c, a);` and `s.lock(fn [](c: &mut Counter) { … })`. The view exists only inside the lock, so it is never held across another lock. Copying the handle with `.share()` is explicit; the data lives as long as its allocator.
- **Channels.** `task.Channel(T)` with `init(capacity, a)`, `send(value)` (moving it) and `recv() -> ?T`, which gives `none` once the channel is closed, with `close()`, and empty. Channels are bounded: a full channel blocks the sender, an empty one the receiver. Sending on a closed channel panics.
- **No async/await.** Tasks are scheduled by the runtime; a task that waits for a scope, a channel or a lock lets another run, and when every task waits for another the program panics: `deadlock: every task is waiting`. The runtime runs tasks as fibers on one thread. `std`'s sockets do not block it: a task that waits for one lets the others run, and when every task waits for a socket the runtime waits for the kernel (`poll`). Other blocking calls, files and `sleep_ns`, still block every task, and tasks do not yet run on several processors at once. `task.yield_now()` lets others run.
- **Determinism.** Which ready task runs next comes from a generator with a seed: the same seed, the same order. `arche test` gives each test a seed of its own, derived from the one it prints, so a failure repeats with `--seed`; other programs start with a fixed seed, and `task.reseed(n)` sets one. A test reaches the capabilities, tasks included, through `t.system()`.

## 10. The low level

### 10.1 `unsafe`

`unsafe { … }` marks code whose correctness the compiler cannot check. Only inside it:

- **Raw pointers** `*T` and `*mut T`: made from views with `&x as *T` (`*mut T` from `&mut x`), from another pointer with `p as *U`, or from integers with `ptr.from_addr(T, n)`; read and written with `p.*`, offset with `p.add(n)` (in elements), and turned back into views with `p.as_ref()` and, for `*mut T`, `p.as_mut()`. Raw pointers are copyable values and may be stored anywhere, outside `unsafe` too; the rules of A§5.4 do not apply to them, and `==` compares their addresses.
- Calls to `unsafe fn`, foreign functions (`extern "c" fn`) and the operations below. An `unsafe fn`'s body is unsafe code.
- Any of these outside `unsafe` is E0921.
- Undefined behaviour is possible only inside `unsafe`: outside it, every program either does what the specification says or traps.

Every `unsafe` block carries a comment that begins `// SAFETY:` and says why it is correct; a block without one is an error in a package that sets `strict_unsafe` (E0920).

### 10.2 Operations for kernels and drivers

These exist for the `freestanding` target (S16) and are all `unsafe`:

| Feature | Form |
| --- | --- |
| Inline assembly | `asm("mov %0, cr3", in(reg) addr)` with typed operands and clobbers; adjacent strings are one text, so assembly can span lines: `in(r) e`, `out(r) p`, `inout(r) p` and `clobber("name")`, where `r` is `reg` (any register) or a register in quotes, `"rax"`; `%N` in the text is the N-th operand, `%%` a `%`. Operands are integers, `bool`s or pointers; `out` and `inout` ones are writable places (E0922). An `asm` also clobbers memory, so the compiler keeps loads and stores in order around it |
| Naked functions | `naked fn`: no prologue or epilogue; it takes no parameters, returns nothing, and its body is one `asm` statement (E0923) |
| Volatile access | `p.read_volatile()`, `p.write_volatile(v)` on a raw pointer to a scalar; `p.addr()` is its address and `core.ptr.from_addr(T, n)` a pointer to address `n` |
| Foreign functions | `extern "c" fn name(x: u64) -> u64;` declares a function of another object file, called by its own name; only integers, `bool`s, floats and raw pointers cross it (E0924). `export("name") fn …` gives an Arche function that name |
| Atomics | `core.atomic.load(T, p, order: o)`, `store`, `swap`, `fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or` and `compare_exchange(T, p, expected: e, new: n, order: o)` on raw pointers to integers (and, without `fetch_`, to raw pointers), with an explicit `Ordering` on every operation: `Relaxed`, `Acquire`, `Release`, `AcqRel`, `SeqCst`. The ordering is written as a constant, and a load cannot release, nor a store acquire (E0927). `fetch_add` and `fetch_sub` wrap. The types `core.atomic.U32` and friends, which need no `unsafe`, come with concurrency (§9) |
| Barriers | `core.atomic.fence(o)`; a `Relaxed` one is E0927 |
| Layout | `packed struct`: no padding, alignment 1, and a view of its fields is an error, since it may be unaligned (E0926); `align(N)` after `struct` or a field's type raises the alignment to `N`, a power of two up to 4096 (E0925); `extern struct` for C's layout, which for fields of the types Arche has is the layout it already uses (docs/abi.md) |
| Sections | `section(".text.boot")` on a function (on a constant: not yet) |
| Panic handler | `panic_handler: "kernel.panic.halt"` in the manifest names a function `fn(message: str, file: str, line: u32, column: u32) -> noreturn` that a panic calls, for targets without `std` (E0928). `arche --panic-handler kernel.panic.halt` names it too, and wins over the manifest |
| Target features | `target_features: ["-sse", "-avx"]` in the manifest |

### 10.3 Targets

`std` exists for hosted targets (Linux, Windows and WASI) and Arche OS. The `freestanding` target has `core` and `alloc` only: no capabilities, and a panic handler supplied by the program.

- `arche --target linux-x86_64`, the default, `--target linux-aarch64`, `--target linux-riscv64`, `--target windows-x86_64` and `--target wasm32-wasi` build for a host. The language, the library and the ABI are the same on all of them (docs/abi.md): a program that imports `std` says nothing about the system under it, since the compiler reads the module `std` from the file of the target, `lib/std.arche` or `lib/std-windows.arche`. Each is written over a module of its own, `std.linux` on system calls and `std.win32` on Win32. The two differ where the systems do, and that is said where it is so: Windows has no signals, so `Ending.Signaled` never comes back from `Proc.spawn`; a program started there ends with everything it started, which is its job on Windows and its process group on Linux; and reading standard input waits for the whole program there, since only sockets are polled.
- `arche --target freestanding-x86_64`, `--target freestanding-riscv64` and `--target freestanding-aarch64` build for a machine without an operating system. Such a program is full Arche, imports neither `std` (E0308) nor anything that does, and has no `main`: its entry is a function it exports, `export("_start")`, usually `naked`.
- Its only allocators are its regions (§7): their arenas come from a static heap that the freestanding runtime (`compiler/runtime/freestanding.c`) treats as a stack, since regions nest. The runtime also gives the code `memcpy` and its relatives, and turns traps into calls of the panic handler.
- The repository's `freestanding/` has a program for each of these targets that boots in QEMU, writes over the serial port, allocates in a region and formats with `core`, and ends in a panic that its handler reports; `freestanding/run.sh` builds and runs them.

## 11. The memory model

This chapter collects every rule of Arche's memory model in one place. Each has its own error code. There is no garbage collector, there are no lifetime annotations, and outside `unsafe` no program can read freed memory or race.

### 11.1 Values and moves (A§5.5)

| Rule | Code |
| --- | --- |
| A move-only value (a container, a capability, a `Shared(T)`, a `core.Unique`, a closure that captures a move-only value, or anything holding one) is moved when it is passed, assigned, returned, stored, captured by value or matched by value; the source is unusable on every path that follows until it is reassigned. A library type that owns memory holds a `core.Unique` | E0605 |
| Nothing is moved out of a place reached through a view; copy it, or move the whole value the view comes from | E0606 |
| Moving a field moves only that field; the rest of the struct stays usable, but the struct as a whole does not until the field is reassigned | E0605 |

### 11.2 Views (A§5.4)

| Rule | Code |
| --- | --- |
| A view (`&T`, `&mut T`, `[]T`, `[]mut T`, `str`, `&dyn I`, a closure that captures a view) is only ever the type of a parameter, a local, a result or the target of a reference: never a field, payload, element, or payload of `?` or `!` | E0601 |
| A function returns a view only with at most one view parameter; the result is a projection of it, or of static memory | E0602 |
| A view never outlives what it views: it is not returned, stored in an outer binding, captured by a task, or bound to a longer-lived region, when what it views does not live that long | E0603 |
| While a view of a place is live, the place is not written or viewed `&mut` through another path; while a `&mut` view is live, the place is not used through another path at all. The views given to one call are live during the call | E0604 |
| A view chosen by an `if` or `match` views one place, or static memory, in every branch (§3.1) | E0607 |

Liveness: a view is live from where it is made to its last use, and through the rest of any loop that uses it. Paths are compared by their fields and elements; elements of one array, list or slice are one path.

### 11.3 Regions, tasks and closures

| Rule | Code |
| --- | --- |
| A value bound to a region does not leave the region's block (§7) | E0901 |
| A task captures a `&mut` view only if neither another task nor its scope uses that place while the task runs (§9) | E0902 |
| A task views only what outlives its scope, and its closure is written where it is spawned (§9) | E0603, E0903 |
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

type_decl   = "type" IDENT [ "(" cparams ")" ] "=" type_body ;
type_body   = [ "packed" | "extern" ] "struct" [ where ] [ align ] "{" { member } "}"   (* a field may end with [ align ] *)
            | "enum" [ where ] "{" variant { "," variant } [ "," ] { member } "}"
            | "error" "{" IDENT { "," IDENT } [ "," ] "}"
            | "interface" "{" { fn_sig ";" } "}"
            | type ";" ;
cparams     = cparam { "," cparam } [ "," ] ;
cparam      = "comptime" IDENT ":" type ;
align       = "align" "(" expr ")" ;
impl_decl   = "impl" named "for" type "{" { [ "pub" ] fn_decl } "}" ;

fn_decl     = fn_sig { contract } block ;
fn_sig      = { fn_mod | fn_attr } "fn" IDENT "(" [ params ] ")" [ "->" type ] [ where ] ;
fn_attr     = ( "export" | "section" ) "(" STRING ")" ;   (* contextual words *)
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
targ        = type | expr ;   (* an INT, CHAR, STRING, '-', 'true' or 'false' starts an expr *)

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
            | "for" for_bind "in" expr [ ( ".." | "..=" ) expr ] block ;
for_bind    = IDENT [ ":" type ] | "(" IDENT "," IDENT ")" ;

expr        = or_expr [ ( "orelse" | "catch" ) expr ] ;
primary     = INT | FLOAT | CHAR | STRING | "true" | "false" | "none"
            | "[" expr ";" expr "]"
            | "some" "(" expr ")"
            | IDENT
            | "(" expr ")"
            | "[" [ expr { "," expr } [ "," ] ] "]"
            | "if" expr value_block "else" ( "if" if_expr_rest | value_block )
            | "match" expr "{" { pattern { "|" pattern } "=>" value_block [ "," ] } "}"
            | "fn" "[" [ capture { "," capture } [ "," ] ] "]" "(" [ params ] ")" [ "->" type ] block
            | "asm" "(" STRING { STRING } { "," asm_operand } ")" ;
if_expr_rest = expr value_block "else" ( "if" if_expr_rest | value_block ) ;
capture     = [ "&" [ "mut" ] ] IDENT ;
asm_operand = ( "in" | "out" | "inout" ) "(" IDENT ")" expr | "clobber" "(" STRING ")" ;
```

The other rules of A§14 (`if_rest`, `simple`, `assign`, `arm`, `pattern`, the operator levels and `postfix`) are unchanged. `FLOAT` is a decimal literal with a `.` or an exponent (§2.1). `for`, `in`, `interface`, `dyn`, `comptime`, `region`, `requires`, `ensures`, `where`, `nopanic`, `naked`, `unsafe` and `asm`, reserved in Arche-0 (A§2.1), are keywords here.

`impl`, `extern`, `packed`, `align`, `export`, `section` and `Self` are **contextual**: they mean what this chapter says only where the grammar puts them (`impl` at the start of an item, `extern` before a string or `struct`, `packed` before `struct`, `align` after `struct` or in a field, `Self` inside an interface or an `impl`). Everywhere else they are ordinary identifiers, so no Arche-0 program that uses them as names changes meaning. Inside `asm(…)`, `in`, `out`, `inout` and `clobber` are contextual the same way.

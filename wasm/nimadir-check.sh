#!/bin/sh
# Nimadir's mini-apps on arche-wasm (docs/sprints.md S31): counter, hello,
# gallery and store run unrebuilt in a shell with no window, and say what
# Nimadir's own shell says to the same cases of its test suite
# (tests/nimadir/cases.txt, expected/); so do they once their programs are
# made components again by arche-wasm's encoder (S38), which refuses a world
# that lacks what a program imports. runaway is stopped by the shell's
# limits: a second a frame, 512 MiB of memory, and what a frame may send.
# An app that imports what the shell does not give is refused. Each runs
# in the interpreter, and again with the JIT (--jit).
# Usage: nimadir-check.sh <arche-wasm>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
wasm=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
apps=$here/tests/nimadir
failed=0

# The apps where Nimadir's registry says they are, so that both shells are
# run alike from one directory.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for app in counter counter-cs hello store gallery runaway; do
    mkdir -p "$work/mini-apps/$app"
    cp "$apps/$app.component.wasm" "$work/mini-apps/$app/"
done

# The same as Nimadir: each case, normalized, against what Nimadir said.
# Then again with each app's program taken out of its component and made a
# component again by arche-wasm's own encoder (lib/wasm/encode.arche).
check_cases() {
grep -v '^#' "$apps/cases.txt" | {
    while read -r name args; do
        [ -n "$name" ] || continue
        for mode in "" --jit; do
            # shellcheck disable=SC2086
            if ! (cd "$work" && "$wasm" nimadir $args $mode --registry "$apps/registry.list") > "$work/out" 2>&1; then
                echo "FAIL $name $mode: arche-wasm failed:"
                head -20 "$work/out"
                failed=$((failed + 1))
                continue
            fi
            sh "$apps/normalize.sh" < "$work/out" > "$work/got"
            cp "$apps/expected/$name.txt" "$work/want"
            if [ -n "$1" ]; then
                # The components are this encoder's, not wit-component's: their
                # sizes are their own.
                sed -i 's/loaded [0-9]* bytes/loaded N bytes/' "$work/got" "$work/want"
            fi
            if ! diff -u "$work/want" "$work/got" > "$work/diff"; then
                echo "FAIL $name $mode $1: not what Nimadir says:"
                head -30 "$work/diff"
                failed=$((failed + 1))
            else
                echo "PASS $name $mode $1: as Nimadir"
            fi
        done
    done
    echo "$failed" > "$work/failed"
}
failed=$(cat "$work/failed")
}
check_cases ""
# counter-cs is not encoded again: its program imports WASI, which
# Nimadir's world does not have; it runs as .NET's toolchain made it.
for app in counter hello store gallery runaway; do
    "$wasm" core "$apps/$app.component.wasm" "$work/$app.core.wasm"
    "$wasm" component "$work/$app.core.wasm" "$apps/world.wit" mini-app "$work/mini-apps/$app/$app.component.wasm"
done
check_cases "(encoded again)"
# What the world does not have, the encoder refuses, saying what.
grep -v 'ui-button: func' "$apps/world.wit" > "$work/less.wit"
if "$wasm" component "$work/counter.core.wasm" "$work/less.wit" mini-app "$work/less.wasm" 2> "$work/err"; then
    echo "FAIL a world without ui-button made the counter a component"
    failed=$((failed + 1))
elif ! grep -qxF "arche-wasm: the world imports no nimadir:shell/host-api@1.2.0 ui-button" "$work/err"; then
    echo "FAIL a world without ui-button, said:"
    cat "$work/err"
    failed=$((failed + 1))
else
    echo "PASS a world without ui-button is refused for the counter"
fi

# An app written in Arche (tests/arche-app, S38): built for wasm32-component
# by the compiler, made a component by the encoder, it keeps its state
# between frames and allocates.
arche=${ARCHE:-$here/../compiler/build/arche}
if ! "$arche" --target wasm32-component build "$here/tests/arche-app/main.arche" "$work/app.core.wasm" > "$work/err" 2>&1 ||
    ! "$wasm" component "$work/app.core.wasm" "$apps/world.wit" mini-app "$work/app.wasm" >> "$work/err" 2>&1; then
    echo "FAIL the Arche app did not build:"
    head -20 "$work/err"
    failed=$((failed + 1))
else
    for mode in "" --jit; do
        "$wasm" nimadir "$work/app.wasm" --frames 3 --input 1:0=Ada $mode > "$work/out" 2>&1 || true
        if grep -qxF '   label   : "Kadr: 3"' "$work/out" && [ "$(grep -c 'Salom, Arche' "$work/out")" -eq 3 ] &&
            [ "$(grep -cxF '   label   : "Typed: Ada"' "$work/out")" -eq 2 ]; then
            echo "PASS the Arche app counts its frames and keeps the text the host hands back $mode"
        else
            echo "FAIL the Arche app $mode:"
            head -20 "$work/out"
            failed=$((failed + 1))
        fi
    done
fi

# Nimadir's counter written in Arche (tests/arche-counter, S38), calling
# the world through the modules arche-wasm bindgen makes of it, generated
# here rather than kept, so that they are always this bindgen's: it says
# what Nimadir's shell says to the Rust one.
mkdir -p "$work/arche-counter"
cp "$here/tests/arche-counter/main.arche" "$here/tests/arche-counter/package.arche" "$work/arche-counter/"
if ! "$wasm" bindgen "$apps/world.wit" mini-app "$work/arche-counter" > "$work/err" 2>&1; then
    echo "FAIL bindgen did not generate the bindings:"
    head -20 "$work/err"
    failed=$((failed + 1))
elif ! grep -qxF 'pub fn ui_label(host: &mut std.Host, text: str) {' "$work/arche-counter/host_api.arche" ||
    ! grep -qxF 'extern "wasm:nimadir:shell/host-api@1.2.0" fn ui_button(p0: u32, p1: u32) -> u32;' "$work/arche-counter/host_api_raw.arche" ||
    ! grep -qxF 'pub fn storage_get(host: &mut std.Host, key: str, a: alloc.Allocator) -> ?alloc.String {' "$work/arche-counter/host_api.arche" ||
    ! grep -qxF 'pub fn list_apps(host: &mut std.Host, a: alloc.Allocator) -> alloc.List(AppEntry) {' "$work/arche-counter/host_api.arche" ||
    ! grep -qxF 'pub fn fetch_poll(host: &mut std.Host, request: u32, a: alloc.Allocator) -> ?ResultHttpResponseString {' "$work/arche-counter/host_api.arche" ||
    ! grep -qxF 'pub fn events(host: &mut std.Host, a: alloc.Allocator) -> alloc.List(Event) {' "$work/arche-counter/dom.arche" ||
    grep -q 'not bound yet' "$work/arche-counter/host_api.arche" "$work/arche-counter/dom.arche" ||
    ! [ -f "$work/arche-counter/dom.arche" ] || ! [ -f "$work/arche-counter/dom_raw.arche" ]; then
    echo "FAIL bindgen generated other bindings:"
    head -30 "$work/arche-counter/host_api.arche"
    failed=$((failed + 1))
else
    echo "PASS bindgen generates the bindings of host-api and dom"
    # Options, results, lists and records (tests/arche-bindings): an app
    # that calls every module bindgen generates says what Nimadir's own
    # shell says to it (expected.txt).
    mkdir -p "$work/arche-bindings"
    cp "$here/tests/arche-bindings/main.arche" "$here/tests/arche-bindings/package.arche" "$work/arche-bindings/"
    "$wasm" bindgen "$apps/world.wit" mini-app "$work/arche-bindings"
    if ! "$arche" --target wasm32-component build "$work/arche-bindings/main.arche" "$work/arche-bindings.core.wasm" > "$work/err" 2>&1 ||
        ! "$wasm" component "$work/arche-bindings.core.wasm" "$apps/world.wit" mini-app "$work/arche-bindings.wasm" >> "$work/err" 2>&1; then
        echo "FAIL the generated bindings did not build:"
        head -20 "$work/err"
        failed=$((failed + 1))
    else
        for mode in "" --jit; do
            # shellcheck disable=SC2086
            "$wasm" nimadir "$work/arche-bindings.wasm" --frames 2 --input 1:0=2 $mode --registry "$apps/registry.list" > "$work/out" 2>&1 || true
            sh "$apps/normalize.sh" < "$work/out" > "$work/got"
            if ! diff -u "$here/tests/arche-bindings/expected.txt" "$work/got" > "$work/diff"; then
                echo "FAIL the bindings' options, results, lists and records $mode: not what Nimadir says:"
                head -30 "$work/diff"
                failed=$((failed + 1))
            else
                echo "PASS the bindings' options, results, lists and records $mode: as Nimadir"
            fi
        done
    fi
    if ! "$arche" --target wasm32-component build "$work/arche-counter/main.arche" "$work/arche-counter.core.wasm" > "$work/err" 2>&1 ||
        ! "$wasm" component "$work/arche-counter.core.wasm" "$apps/world.wit" mini-app "$work/arche-counter.wasm" >> "$work/err" 2>&1; then
        echo "FAIL the Arche counter did not build:"
        head -20 "$work/err"
        failed=$((failed + 1))
    else
        for mode in "" --jit; do
            # shellcheck disable=SC2086
            "$wasm" nimadir "$work/arche-counter.wasm" --script 1:0,2:0,3:0,5:1 --frames 8 $mode --registry "$apps/registry.list" > "$work/out" 2>&1 || true
            sh "$apps/normalize.sh" < "$work/out" > "$work/got"
            if ! diff -u "$apps/expected/counter-script.txt" "$work/got" > "$work/diff"; then
                echo "FAIL the Arche counter $mode: not what Nimadir says to the Rust one:"
                head -30 "$work/diff"
                failed=$((failed + 1))
            else
                echo "PASS the Arche counter $mode: as Nimadir to the Rust one"
            fi
        done
    fi
fi

# A component's root module without update is refused (E0313).
if "$arche" --target wasm32-component build "$here/tests/arche-app-no-update/main.arche" "$work/no-update.wasm" > "$work/err" 2>&1; then
    echo "FAIL a component without update was built"
    failed=$((failed + 1))
elif ! grep -q 'error\[E0313\]' "$work/err"; then
    echo "FAIL a component without update, said:"
    head -5 "$work/err"
    failed=$((failed + 1))
else
    echo "PASS a component without update is refused (E0313)"
fi

# expect <description> <status> <line that must appear> -- <arche-wasm args>...
expect() {
    what=$1
    want_status=$2
    want_line=$3
    shift 4
    app=$1
    shift
    for mode in "" --jit; do
        set +e
        out=$("$wasm" nimadir "$app" $mode "$@" 2>&1)
        status=$?
        set -e
        if [ "$status" -ne "$want_status" ] || ! printf '%s\n' "$out" | grep -qxF -- "$want_line"; then
            echo "FAIL $what $mode (status $status, wanted $want_status and '$want_line'):"
            printf '%s\n' "$out" | head -20
            failed=$((failed + 1))
        else
            echo "PASS $what $mode"
        fi
    done
}

expect "runaway spinning is stopped by the frame's deadline" 3 "stopped: deadline exceeded" -- "$apps/runaway.component.wasm" --frames 2 --script 1:0
expect "runaway eating memory is stopped at 512 MiB" 3 "stopped: memory limit exceeded" -- "$apps/runaway.component.wasm" --frames 2 --script 1:1
expect "runaway flooding the shell is stopped" 3 "stopped: host limit exceeded" -- "$apps/runaway.component.wasm" --frames 2 --script 1:2
expect "a full store is said, not a trap" 0 "   [wasm log] runaway: storage is full: 1 MiB per app" -- "$apps/runaway.component.wasm" --frames 2 --script 1:3
expect "an import the shell does not give is refused" 1 "arche-wasm: the component did not instantiate: imports what the shell does not give" -- "$apps/counter.component.wasm" --deny now-millis

# The app template (template/, S38), built by nimadir-app.sh as an app
# author would, and checked as an app's own repository checks it
# (app-check.sh): its button counts, by the interpreter and by the JIT,
# as Nimadir's shell said to the same taps (template/expected.txt).
if ! ARCHE="$arche" ARCHE_WASM="$wasm" sh "$here/nimadir-app.sh" "$here/template" "$work/template.wasm" > "$work/err" 2>&1; then
    echo "FAIL the template did not build:"
    head -20 "$work/err"
    failed=$((failed + 1))
elif ARCHE="$arche" ARCHE_WASM="$wasm" sh "$here/app-check.sh" "$here/template" > "$work/check" 2>&1; then
    echo "PASS the template counts its taps as Nimadir's shell, by app-check.sh"
else
    echo "FAIL the template, by app-check.sh:"
    head -30 "$work/check"
    failed=$((failed + 1))
fi

# A world that includes Nimadir's and imports more (tests/extension), as
# the phone's does: its WIT a directory with Nimadir's in deps/, its
# include and its other package's interfaces read, bound and encoded; the
# functions the embedder adds (--add) answered and their calls said;
# woken, the app posts, drawn, it cancels, its time the one --now-millis
# gives; without them, it is refused.
mkdir -p "$work/ext-wit/deps/nimadir"
cp "$here/tests/extension/ext.wit" "$work/ext-wit/"
cp "$apps/world.wit" "$work/ext-wit/deps/nimadir/"
if ! ARCHE="$arche" ARCHE_WASM="$wasm" WIT="$work/ext-wit" WORLD=app sh "$here/nimadir-app.sh" "$here/tests/extension" "$work/ext.wasm" > "$work/err" 2>&1; then
    echo "FAIL the app of a world that includes Nimadir's did not build:"
    head -20 "$work/err"
    failed=$((failed + 1))
else
    for woken in false true; do
        expected=$here/tests/extension/expected.txt
        frames="--frames 3 --script 2:0"
        if [ "$woken" = true ]; then
            expected=$here/tests/extension/expected-woken.txt
            frames="--frames 1"
        fi
        for mode in "" --jit; do
            # shellcheck disable=SC2086
            "$wasm" nimadir "$work/ext.wasm" $frames $mode --now-millis 1794294000000 --add 'test:ext/notes@0.1.0#post=numbered' --add 'test:ext/notes@0.1.0#cancel' --add "test:ext/wake@0.1.0#woken=$woken" --add 'test:ext/inbox@0.1.0#list=[]' --add 'test:ext/inbox@0.1.0#located=none' > "$work/out" 2>&1 || true
            sh "$apps/normalize.sh" < "$work/out" > "$work/got"
            if diff -u "$expected" "$work/got" > "$work/diff"; then
                echo "PASS an app of a world that includes Nimadir's, woken $woken $mode"
            else
                echo "FAIL an app of a world that includes Nimadir's, woken $woken $mode:"
                head -30 "$work/diff"
                failed=$((failed + 1))
            fi
        done
    done
    if "$wasm" nimadir "$work/ext.wasm" --frames 1 > "$work/out" 2>&1; then
        echo "FAIL an app that imports what the shell does not give ran"
        failed=$((failed + 1))
    elif grep -qF "imports what the shell does not give" "$work/out"; then
        echo "PASS an app that imports what no one added is refused"
    else
        echo "FAIL an app that imports what no one added:"
        head -5 "$work/out"
        failed=$((failed + 1))
    fi
fi

# A folder the embedder shares (tests/files, --share, nimadir.share), as
# the phone shares its files: the app lists it, reads, writes, makes a
# folder and removes a file in it, by the interpreter and by the JIT, each
# from the same folder; what leaves it is refused, and the file beside it
# is not read. Afterwards the folder holds what the app wrote.
mkdir -p "$work/files-wit/deps/nimadir"
cp "$here/tests/files/files.wit" "$work/files-wit/"
cp "$apps/world.wit" "$work/files-wit/deps/nimadir/"
if ! ARCHE="$arche" ARCHE_WASM="$wasm" WIT="$work/files-wit" WORLD=app sh "$here/nimadir-app.sh" "$here/tests/files" "$work/files.wasm" > "$work/err" 2>&1; then
    echo "FAIL the app of a shared folder did not build:"
    head -20 "$work/err"
    failed=$((failed + 1))
else
    for mode in "" --jit; do
        rm -rf "$work/share"
        mkdir -p "$work/share/shared/docs" "$work/share/shared/pics"
        echo hello > "$work/share/shared/docs/hello.txt"
        echo old > "$work/share/shared/docs/old.txt"
        echo secret > "$work/share/outside.txt"
        # shellcheck disable=SC2086
        (cd "$work/share" && "$wasm" nimadir "$work/files.wasm" --frames 1 $mode --share test:files/files@0.1.0=shared) > "$work/out" 2>&1 || true
        sh "$apps/normalize.sh" < "$work/out" > "$work/got"
        left=$(cd "$work/share" && find shared -print | sort | tr '\n' ' ')
        if diff -u "$here/tests/files/expected.txt" "$work/got" > "$work/diff" && [ "$left" = "shared shared/docs shared/docs/hello.txt shared/docs/new.txt shared/made shared/pics " ]; then
            echo "PASS a folder shared $mode"
        else
            echo "FAIL a folder shared $mode, the folder left as: $left"
            head -30 "$work/diff"
            failed=$((failed + 1))
        fi
    done
fi

# Every kind of WIT type bindgen binds (tests/echo): an app given each,
# through its bindings, to a function of echo.wit that arche-wasm echo
# answers with what it was given, by the runtime's canonical ABI, and
# compared with what comes back.
mkdir -p "$work/echo"
cp "$here/tests/echo/main.arche" "$here/tests/echo/package.arche" "$work/echo/"
if ! "$wasm" bindgen "$here/tests/echo/echo.wit" echo-app "$work/echo" > "$work/err" 2>&1 ||
    ! "$arche" --target wasm32-component build "$work/echo/main.arche" "$work/echo.core.wasm" >> "$work/err" 2>&1 ||
    ! "$wasm" component "$work/echo.core.wasm" "$here/tests/echo/echo.wit" echo-app "$work/echo.wasm" >> "$work/err" 2>&1; then
    echo "FAIL bindgen's types did not build:"
    head -20 "$work/err"
    failed=$((failed + 1))
elif ! "$wasm" echo "$work/echo.wasm" > "$work/out" 2>&1 || ! diff -u "$here/tests/echo/expected.txt" "$work/out" > "$work/diff"; then
    echo "FAIL bindgen's types do not come back as they went:"
    head -30 "$work/diff" "$work/out"
    failed=$((failed + 1))
else
    echo "PASS bindgen's records, variants, enums, flags, options, results and tuples come back as they went"
fi

# Every component the encoder made, validated by wasm-tools where there is
# one; CI requires it (WASM_TOOLS_REQUIRED=1).
if command -v wasm-tools > /dev/null 2>&1; then
    for f in "$work"/mini-apps/*/*.component.wasm "$work/app.wasm" "$work/template.wasm" "$work/echo.wasm" "$work/ext.wasm"; do
        if wasm-tools validate "$f" 2> "$work/err"; then
            echo "PASS wasm-tools validates $(basename "$f")"
        else
            echo "FAIL wasm-tools does not validate $(basename "$f"):"
            head -5 "$work/err"
            failed=$((failed + 1))
        fi
    done
elif [ "${WASM_TOOLS_REQUIRED:-0}" = 1 ]; then
    echo "FAIL no wasm-tools, and WASM_TOOLS_REQUIRED is set"
    failed=$((failed + 1))
else
    echo "SKIP wasm-tools validate: no wasm-tools here"
fi

if [ "$failed" -ne 0 ]; then
    echo "nimadir-check: $failed failed"
    exit 1
fi
echo "nimadir-check: all passed"

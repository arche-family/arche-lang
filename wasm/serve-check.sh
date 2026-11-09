#!/usr/bin/env bash
# WASM handlers over HTTPS (docs/sprints.md S33): arche-wasm serve answers
# each request by tests/handler/handler.wasm in its sandbox, over TLS 1.3 and
# HTTP/2. A handler that spins is stopped by its 100 ms, one that grows
# memory at 64 MiB, each with a 503, and the next request gets a new
# instance; a module that imports what the server does not give is refused.
# The modules are assembled from their .wat by wabt (spec.sh fetches it).
# Usage: serve-check.sh <arche-wasm> [--jit]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
wasm=$1
wat2wasm=${WAT2WASM:-$here/build/spec/wabt/usr/bin/wat2wasm}
work=$(mktemp -d)
cleanup() {
    jobs -p | xargs -r kill 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
"$wat2wasm" "$here/tests/handler/handler.wat" -o "$work/handler.wasm"
"$wat2wasm" "$here/tests/handler/refused.wat" -o "$work/refused.wasm"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$work/key" -out "$work/cert" \
    -subj /CN=localhost -days 1 -addext "subjectAltName=DNS:localhost" 2>/dev/null

passed=0
failed=0
check() { # name expected actual
    if [ "$2" = "$3" ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        echo "FAIL $1: expected '$2', got '$(printf %s "$3" | head -c 300)'"
    fi
}

port=$((20000 + RANDOM % 20000))
"$wasm" serve "$work/handler.wasm" "$port" "$work/cert" "$work/key" 1 > "$work/out" 2> "$work/err" &
for _ in $(seq 100); do
    grep -q ready "$work/out" 2>/dev/null && break
    sleep 0.05
done
url=https://localhost:$port
c=(curl -s --cacert "$work/cert" --resolve "localhost:$port:127.0.0.1" --tlsv1.3)

check "HTTP/2 over TLS 1.3" "2 200" "$("${c[@]}" --http2 -o /dev/null -w '%{http_version} %{http_code}' "$url/hello")"
check "the handler's answer" "hello from a wasm handler" "$("${c[@]}" "$url/hello")"
check "its content type" "text/plain" "$("${c[@]}" -o /dev/null -w '%{content_type}' "$url/hello")"
check "echo" "a body, back" "$("${c[@]}" -X POST --data-binary 'a body, back' "$url/echo")"
check "the time it is given" "yes" "$(t=$("${c[@]}" "$url/time" | sed 's/[^0-9]//g'); [ "$t" -gt 1700000000000 ] && echo yes || echo "no: $t")"
check "not found" "404" "$("${c[@]}" -o /dev/null -w '%{http_code}' "$url/nothing")"
check "the deadline stops a spin" "the handler stopped: deadline exceeded 503" "$("${c[@]}" -w '%{http_code}' "$url/spin" | tr '\n' ' ')"
check "a new instance after it" "requests: 1" "$("${c[@]}" "$url/count")"
check "the memory limit stops growth" "the handler stopped: memory limit exceeded 503" "$("${c[@]}" -w '%{http_code}' "$url/grow" | tr '\n' ' ')"
check "and after that" "hello from a wasm handler" "$("${c[@]}" "$url/hello")"
check "what it logged" "yes" "$(grep -q 'handler: a request' "$work/err" && echo yes || echo no)"
set +e
"$wasm" serve "$work/refused.wasm" "$((port + 1))" "$work/cert" "$work/key" 1 > /dev/null 2> "$work/refused"
status=$?
set -e
check "an import the server does not give is refused" "1 arche-wasm: the handler cannot be loaded: it imports what the server does not give" "$status $(cat "$work/refused")"
echo "serve check: $passed passed, $failed failed"
[ "$failed" -eq 0 ]

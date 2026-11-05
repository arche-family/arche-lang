#!/usr/bin/env bash
# HTTP interop (docs/sprints.md S22): http_server (Arche) against curl and
# a browser (headless Chrome, when one is installed):
#
#   HTTP/1.1 in the clear: keep-alive, chunked both ways, HEAD, a large
#   upload with Expect: 100-continue, a large download;
#   HTTP/2 in the clear (prior knowledge) and over TLS (ALPN h2): requests
#   multiplexed on one connection, downloads and uploads larger than the
#   flow-control windows;
#   Chrome loading a page over TLS, which it does with HTTP/2.
#
# The certificate and key are made here, by openssl, and thrown away.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
build=${WORK:-$here/../build/interop}
mkdir -p "$build"
work=$(mktemp -d)
cleanup() {
    jobs -p | xargs -r kill 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

"$here/../compiler/arche-build" "$here/http_server.arche" "$build/http_server"

openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$work/ca-key" 2>/dev/null
openssl req -new -x509 -key "$work/ca-key" -subj "/CN=Arche HTTP interop CA" -days 2 -sha256 \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" -out "$work/ca" 2>/dev/null
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$work/leaf-key" 2>/dev/null
openssl req -new -key "$work/leaf-key" -subj "/CN=localhost" -out "$work/leaf.csr" 2>/dev/null
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nsubjectAltName=DNS:localhost,IP:127.0.0.1\n' > "$work/leaf.ext"
openssl x509 -req -in "$work/leaf.csr" -CA "$work/ca" -CAkey "$work/ca-key" -CAcreateserial -days 1 -sha256 \
    -extfile "$work/leaf.ext" -out "$work/leaf" 2>/dev/null

passed=0
failed=0
pass() {
    passed=$((passed + 1))
}
fail() {
    failed=$((failed + 1))
    echo "FAIL $1"
    [ -s "$work/err" ] && sed 's/^/    /' "$work/err" | head -10
    return 0
}
# check <name> <expected> <actual>
check() {
    if [ "$2" = "$3" ]; then pass; else fail "$1: expected '$2', got '$(printf %s "$3" | head -c 200)'"; fi
}

wait_ready() {
    for _ in $(seq 100); do
        if grep -q ready "$work/server-out" 2>/dev/null; then
            return 0
        fi
        sleep 0.05
    done
    echo "the server did not start"
    cat "$work/server-err"
    exit 1
}

port=$((20000 + $$ % 20000))
head -c 300000 /dev/urandom > "$work/upload"
upload_sum=$(sha256sum < "$work/upload" | cut -d' ' -f1)
big_sum=$(python3 -I -c 'import hashlib,sys; n=int(sys.argv[1]); print(hashlib.sha256(bytes(97 + i % 26 for i in range(n))).hexdigest())' 1000000)

# ---- In the clear ----------------------------------------------------------------------------
: > "$work/server-out"
"$build/http_server" $port 0 > "$work/server-out" 2> "$work/server-err" &
server=$!
wait_ready
url=http://127.0.0.1:$port

out=$(curl -sS --http1.1 "$url/" 2> "$work/err" || true)
check "HTTP/1.1 GET" '<!doctype html><html><body><p id="p">hello from arche over HTTP/1.1</p></body></html>' "$out"
out=$(curl -sS --http1.1 -w '%{num_connects}' -o /dev/null -o /dev/null "$url/" "$url/headers" 2> "$work/err" || true)
check "HTTP/1.1 keep-alive: two requests, connections made" "10" "$out"
out=$(curl -sS --http1.1 -H 'Transfer-Encoding: chunked' --data-binary @"$work/upload" "$url/echo" 2> "$work/err" | sha256sum | cut -d' ' -f1)
check "HTTP/1.1 chunked upload" "$upload_sum" "$out"
out=$(curl -sS --http1.1 -H 'Expect: 100-continue' --data-binary @"$work/upload" "$url/echo" 2> "$work/err" | sha256sum | cut -d' ' -f1)
check "HTTP/1.1 upload with 100-continue" "$upload_sum" "$out"
out=$(curl -sS --http1.1 -D - -o /dev/null "$url/chunked" 2> "$work/err" | tr -d '\r' | grep -i '^transfer-encoding' || true)
check "HTTP/1.1 chunked response header" "transfer-encoding: chunked" "$out"
out=$(curl -sS --http1.1 "$url/chunked" 2> "$work/err" || true)
check "HTTP/1.1 chunked response body" "chunked body from arche" "$out"
out=$(curl -sS --http1.1 -I "$url/big?n=1234" 2> "$work/err" | tr -d '\r' | grep -i '^content-length' || true)
check "HTTP/1.1 HEAD" "content-length: 1234" "$out"
out=$(curl -sS --http1.1 "$url/big?n=1000000" 2> "$work/err" | sha256sum | cut -d' ' -f1)
check "HTTP/1.1 1 MB download" "$big_sum" "$out"
out=$(curl -sS --http1.0 "$url/" 2> "$work/err" || true)
check "HTTP/1.0 GET" '<!doctype html><html><body><p id="p">hello from arche over HTTP/1.0</p></body></html>' "$out"
out=$(curl -sS -o /dev/null -w '%{http_code}' "$url/nowhere" 2> "$work/err" || true)
check "404" "404" "$out"
out=$(printf 'GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\n' | nc -q 1 127.0.0.1 $port 2>/dev/null | head -1 | tr -d '\r' || true)
check "HTTP/1.1 smuggling refused" "HTTP/1.1 400 Bad Request" "$out"

# A client that connects and says nothing must not hold up the others.
(sleep 5 | nc 127.0.0.1 $port > /dev/null 2>&1) &
idle=$!
sleep 0.2
out=$(curl -sS --max-time 3 --http1.1 "$url/" 2> "$work/err" | grep -o 'hello from arche' || true)
check "served while another connection idles" "hello from arche" "$out"
kill $idle 2>/dev/null || true

out=$(curl -sS --http2-prior-knowledge "$url/" 2> "$work/err" || true)
check "h2c GET" '<!doctype html><html><body><p id="p">hello from arche over HTTP/2</p></body></html>' "$out"
out=$(curl -sS --http2-prior-knowledge "$url/big?n=1000000" 2> "$work/err" | sha256sum | cut -d' ' -f1)
check "h2c 1 MB download (flow control)" "$big_sum" "$out"
out=$(curl -sS --http2-prior-knowledge --data-binary @"$work/upload" "$url/echo" 2> "$work/err" | sha256sum | cut -d' ' -f1)
check "h2c upload" "$upload_sum" "$out"
out=$(curl -sS --http2-prior-knowledge --parallel --parallel-max 20 -w '%{num_connects}\n' -o /dev/null \
    $(for i in $(seq 20); do printf -- '-o /dev/null %s/big?n=%d ' "$url" $((100000 + i)); done) 2> "$work/err" | sort | uniq -c | tr -s ' ' || true)
check "h2c 20 requests multiplexed on one connection (19 reused it)" "19 0 1 1" "$(echo $out)"
kill $server 2>/dev/null || true
wait $server 2>/dev/null || true

# ---- Over TLS ----------------------------------------------------------------------------------
port=$((port + 1))
: > "$work/server-out"
"$build/http_server" $port 0 "$work/leaf" "$work/leaf-key" > "$work/server-out" 2> "$work/server-err" &
server=$!
wait_ready
url=https://localhost:$port
ca=(--cacert "$work/ca")

out=$(curl -sS "${ca[@]}" "$url/" 2> "$work/err" || true)
check "h2 over TLS (ALPN)" '<!doctype html><html><body><p id="p">hello from arche over HTTP/2</p></body></html>' "$out"
out=$(curl -sS "${ca[@]}" --http1.1 "$url/" 2> "$work/err" || true)
check "HTTP/1.1 over TLS" '<!doctype html><html><body><p id="p">hello from arche over HTTP/1.1</p></body></html>' "$out"
out=$(curl -sS "${ca[@]}" "$url/big?n=1000000" 2> "$work/err" | sha256sum | cut -d' ' -f1)
check "h2 over TLS, 1 MB download" "$big_sum" "$out"
out=$(curl -sS "${ca[@]}" --data-binary @"$work/upload" "$url/echo" 2> "$work/err" | sha256sum | cut -d' ' -f1)
check "h2 over TLS, upload" "$upload_sum" "$out"
out=$(curl -sS "${ca[@]}" --parallel --parallel-max 10 -w '%{num_connects}\n' \
    $(for i in $(seq 10); do printf -- '-o /dev/null %s/big?n=%d ' "$url" $((50000 + i)); done) 2> "$work/err" | sort | uniq -c | tr -s ' ' || true)
check "h2 over TLS, 10 requests multiplexed on one connection (9 reused it)" "9 0 1 1" "$(echo $out)"

chrome=$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)
if [ -n "$chrome" ]; then
    out=$(timeout 60 "$chrome" --headless=new --no-sandbox --disable-gpu --ignore-certificate-errors \
        --user-data-dir="$work/chrome" --dump-dom "$url/" 2> "$work/err" | grep -o 'hello from arche over [^<]*' || true)
    check "Chrome over TLS" "hello from arche over HTTP/2" "$out"
else
    echo "no Chrome: the browser test is skipped"
fi
kill $server 2>/dev/null || true
wait $server 2>/dev/null || true

if [ -s "$work/server-err" ]; then
    sed 's/^/    server: /' "$work/server-err" | head -20
fi
echo "$passed passed, $failed failed"
[ $failed -eq 0 ]

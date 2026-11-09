#!/usr/bin/env bash
# The web application's check (docs/sprints.md S32), with curl:
#   HTTPS with TLS 1.3, over HTTP/2 and HTTP/1.1; files from public/, gzip
#   when asked for; the JSON API; 404 and 405; no file outside public/; two
#   workers answering; and memory that does not grow with requests: one
#   worker's resident memory after 400 requests and after 4000 more.
# The certificate is made here by openssl and thrown away.
# Usage: check.sh [webapp binary]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
app=${1:-$here/build/webapp}
work=$(mktemp -d)
cleanup() {
    jobs -p | xargs -r kill 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$work/ca-key" 2>/dev/null
openssl req -new -x509 -key "$work/ca-key" -subj "/CN=Arche webapp CA" -days 2 -sha256 \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" -out "$work/ca" 2>/dev/null
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$work/key" 2>/dev/null
openssl req -new -key "$work/key" -subj "/CN=localhost" -out "$work/csr" 2>/dev/null
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nsubjectAltName=DNS:localhost,IP:127.0.0.1\n' > "$work/ext"
openssl x509 -req -in "$work/csr" -CA "$work/ca" -CAkey "$work/ca-key" -CAcreateserial -days 1 -sha256 \
    -extfile "$work/ext" -out "$work/cert" 2>/dev/null

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

start() { # port workers
    "$app" "$1" "$here/public" "$work/cert" "$work/key" "$2" > "$work/out-$1" 2> "$work/err-$1" &
    for _ in $(seq 100); do
        if grep -q ready "$work/out-$1" 2>/dev/null; then
            return 0
        fi
        sleep 0.05
    done
    echo "the server did not start"
    cat "$work/err-$1"
    exit 1
}

port=$((20000 + RANDOM % 20000))
start "$port" 2
url=https://localhost:$port
c=(curl -s --cacert "$work/ca" --resolve "localhost:$port:127.0.0.1" --tlsv1.3)

check "HTTP/2 over TLS 1.3" "2 200" "$("${c[@]}" --http2 -o /dev/null -w '%{http_version} %{http_code}' "$url/")"
check "HTTP/1.1 over TLS 1.3" "1.1 200" "$("${c[@]}" --http1.1 -o /dev/null -w '%{http_version} %{http_code}' "$url/")"
"${c[@]}" --http2 "$url/" > "$work/index"
check "the index file" "same" "$(cmp -s "$work/index" "$here/public/index.html" && echo same || echo differs)"
check "a file's type" "text/css; charset=utf-8" "$("${c[@]}" -o /dev/null -w '%{content_type}' "$url/style.css")"
"${c[@]}" --http2 -H 'accept-encoding: gzip' -D "$work/head" "$url/index.html" > "$work/gz"
check "gzip when asked" "content-encoding: gzip" "$(grep -i '^content-encoding' "$work/head" | tr -d '\r' | tr 'A-Z' 'a-z')"
check "gzip's bytes" "same" "$(gzip -dc "$work/gz" | cmp -s - "$here/public/index.html" && echo same || echo differs)"
check "not gzip unasked" "" "$("${c[@]}" -D - -o /dev/null "$url/index.html" | grep -i '^content-encoding' || true)"
check "security headers" "nosniff" "$("${c[@]}" -D - -o /dev/null "$url/" | grep -i '^x-content-type-options' | tr -d '\r' | sed 's/.*: //')"
check "a file that is not there" "404" "$("${c[@]}" -o /dev/null -w '%{http_code}' "$url/nothing.html")"
check "no way out of the directory" "404" "$("${c[@]}" --path-as-is -o /dev/null -w '%{http_code}' "$url/../package.arche")"
check "no hidden files" "404" "$("${c[@]}" -o /dev/null -w '%{http_code}' "$url/.hidden")"
check "a method a route does not take" "405" "$("${c[@]}" -X DELETE -o /dev/null -w '%{http_code}' "$url/api/notes")"
check "health" '{"ok":true' "$("${c[@]}" "$url/api/health" | cut -c1-10)"
check "a bad note" "400" "$("${c[@]}" -o /dev/null -w '%{http_code}' -X POST -d 'not json' "$url/api/notes")"
check "a note that is not there" "404" "$("${c[@]}" -o /dev/null -w '%{http_code}' "$url/api/notes/99")"
seen=$(for _ in $(seq 60); do "${c[@]}" "$url/api/health"; echo; done | sort -u | wc -l)
cpus=$(nproc)
if [ "$cpus" -gt 1 ]; then
    check "two workers answer" "2" "$seen"
fi

# Memory: one worker, so that every request reaches the one measured.
port2=$((port + 1))
start "$port2" 1
url2=https://localhost:$port2
c2=(curl -s --cacert "$work/ca" --resolve "localhost:$port2:127.0.0.1")
load() { # rounds
    for _ in $(seq "$1"); do
        # Twenty requests on one HTTP/2 connection, twenty on one HTTP/1.1
        # connection, and five connections of their own.
        "${c2[@]}" --http2 $(printf "$url2/index.html?%s " $(seq 20)) > /dev/null
        "${c2[@]}" --http1.1 -H "accept-encoding: gzip" $(printf "$url2/api/health?%s " $(seq 20)) > /dev/null
        for _ in 1 2 3 4 5; do
            "${c2[@]}" -o /dev/null "$url2/api/notes"
        done
    done
}
rss() {
    "${c2[@]}" "$url2/api/memory" | sed 's/[^0-9]//g'
}
# Notes are each worker's own: this server has one.
check "notes" '201 201 [{"id":0,"text":"first"},{"id":1,"text":"second \"quoted\""}]' \
    "$("${c2[@]}" -w '%{http_code} ' -X POST -H 'content-type: application/json' -d '{"text":"first"}' -o /dev/null "$url2/api/notes")$("${c2[@]}" -w '%{http_code} ' -X POST -d '{"text":"second \"quoted\""}' -o /dev/null "$url2/api/notes")$("${c2[@]}" "$url2/api/notes")"
check "one note" '{"id":1,"text":"second \"quoted\""}' "$("${c2[@]}" "$url2/api/notes/1")"
load 9
before=$(rss)
load 90
after=$(rss)
grown=$((after - before))
echo "resident memory: $before KiB after 400 requests, $after KiB after 4050 more"
check "memory does not grow with requests" "yes" "$([ "$grown" -lt 64 ] && echo yes || echo "no: $grown KiB more")"

echo "webapp check: $passed passed, $failed failed"
[ "$failed" -eq 0 ]

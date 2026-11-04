#!/usr/bin/env bash
# TLS 1.3 interop (docs/sprints.md S21): lib/tls against OpenSSL and curl.
#
#   1. tls_client (Arche) against openssl s_server;
#   2. openssl s_client against tls_server (Arche);
#   3. curl against tls_server;
#
# for each cipher suite and key exchange group, with P-256 and Ed25519
# certificates. The keys and certificates are made here, by openssl, and
# thrown away at the end. Needs openssl 3 and curl.
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

"$here/../compiler/arche-build" "$here/tls_client.arche" "$build/tls_client"
"$here/../compiler/arche-build" "$here/tls_server.arche" "$build/tls_server"

cat > "$work/leaf.ext" <<'EOF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1
EOF
for kind in ec ed; do
    if [ $kind = ec ]; then
        gen=(-algorithm EC -pkeyopt ec_paramgen_curve:P-256)
        md=(-sha256)
    else
        gen=(-algorithm ED25519)
        md=()
    fi
    openssl genpkey "${gen[@]}" -out "$work/$kind-ca-key" 2>/dev/null
    openssl req -new -x509 -key "$work/$kind-ca-key" -subj "/CN=Arche interop CA $kind" -days 2 "${md[@]}" \
        -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" -out "$work/$kind-ca" 2>/dev/null
    openssl genpkey "${gen[@]}" -out "$work/$kind-leaf-key" 2>/dev/null
    openssl req -new -key "$work/$kind-leaf-key" -subj "/CN=localhost" -out "$work/$kind-leaf.csr" 2>/dev/null
    openssl x509 -req -in "$work/$kind-leaf.csr" -CA "$work/$kind-ca" -CAkey "$work/$kind-ca-key" -CAcreateserial \
        -days 1 "${md[@]}" -extfile "$work/leaf.ext" -out "$work/$kind-leaf" 2>/dev/null
done

port=$((20000 + $$ % 20000))
passed=0
failed=0
check() {
    if grep -q "$2" "$work/out"; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        echo "FAIL $1"
        sed 's/^/    /' "$work/out" | head -20
        sed 's/^/    /' "$work/err" 2>/dev/null | head -20
    fi
}

# Waits until the file has the word: the server is listening.
wait_for() {
    for _ in $(seq 100); do
        if grep -q "$2" "$1" 2>/dev/null; then
            return 0
        fi
        sleep 0.05
    done
    return 1
}

suites="TLS_AES_128_GCM_SHA256 TLS_AES_256_GCM_SHA384 TLS_CHACHA20_POLY1305_SHA256"
groups="X25519 P-256"

# 1. The Arche client against openssl s_server.
for kind in ec ed; do
    for suite in $suites; do
        for group in $groups; do
            port=$((port + 1))
            : > "$work/server-out"
            openssl s_server -accept $port -cert "$work/$kind-leaf" -key "$work/$kind-leaf-key" -www -tls1_3 \
                -ciphersuites $suite -groups $group -naccept 1 > "$work/server-out" 2>&1 &
            wait_for "$work/server-out" ACCEPT
            "$build/tls_client" $port localhost "$work/$kind-ca" > "$work/out" 2> "$work/err" || true
            check "tls_client -> openssl s_server ($kind, $suite, $group)" "HTTP/1.0 200 ok"
            wait 2>/dev/null || true
        done
    done
done

# The Arche client must refuse a certificate for another name, and one
# from a CA it does not trust.
for case in wrong-name wrong-ca; do
    port=$((port + 1))
    : > "$work/server-out"
    openssl s_server -accept $port -cert "$work/ec-leaf" -key "$work/ec-leaf-key" -www -tls1_3 -naccept 1 \
        > "$work/server-out" 2>&1 &
    wait_for "$work/server-out" ACCEPT
    host=localhost
    ca="$work/ec-ca"
    if [ $case = wrong-name ]; then host=example.com; fi
    if [ $case = wrong-ca ]; then ca="$work/ed-ca"; fi
    if "$build/tls_client" $port $host "$ca" > "$work/out" 2> "$work/err"; then
        failed=$((failed + 1))
        echo "FAIL tls_client accepted a bad certificate ($case)"
    else
        passed=$((passed + 1))
    fi
    wait 2>/dev/null || true
done

# 2 and 3. openssl s_client and curl against the Arche server.
for kind in ec ed; do
    port=$((port + 1))
    count=$((3 * 2 + 2))
    : > "$work/server-out"
    cat "$work/$kind-leaf" > "$work/$kind-chain"
    "$build/tls_server" $port "$work/$kind-chain" "$work/$kind-leaf-key" $count > "$work/server-out" 2> "$work/server-err" &
    wait_for "$work/server-out" ready
    for suite in $suites; do
        for group in $groups; do
            printf 'GET / HTTP/1.0\r\n\r\n' | openssl s_client -connect 127.0.0.1:$port -servername localhost \
                -CAfile "$work/$kind-ca" -verify_return_error -verify_hostname localhost -tls1_3 \
                -ciphersuites $suite -groups $group -quiet > "$work/out" 2> "$work/err" || true
            check "openssl s_client -> tls_server ($kind, $suite, $group)" "hello from arche"
        done
    done
    curl -sS --cacert "$work/$kind-ca" --tlsv1.3 https://localhost:$port/ > "$work/out" 2> "$work/err" || true
    check "curl -> tls_server ($kind)" "hello from arche"
    curl -sS --cacert "$work/$kind-ca" --tlsv1.3 --http1.1 https://127.0.0.1:$port/ > "$work/out" 2> "$work/err" || true
    check "curl -> tls_server ($kind, by address)" "hello from arche"
    wait 2>/dev/null || true
    if [ -s "$work/server-err" ]; then
        sed 's/^/    server: /' "$work/server-err"
    fi
done

echo "$passed passed, $failed failed"
[ $failed -eq 0 ]

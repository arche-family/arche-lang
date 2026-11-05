#!/usr/bin/env bash
# The package manager's acceptance test (docs/sprints.md S25):
#
#   - a library is published to a registry and an application adds it, with
#     the library's own dependency, over HTTPS from arche-registry;
#   - the same sources and lockfile give byte-identical programs;
#   - a dependency that uses a capability its manifest does not need is
#     refused (E0801);
#   - an archive whose hash, or a release record whose signature, does not
#     match is refused, and so is a registry signed with another key;
#   - a published version cannot be published again.
#
# The TLS certificate is made by openssl and thrown away.
# Usage: pkg-check.sh <arche-driver> <arche-registry>
set -eu
driver=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
registry_server=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
work=$(mktemp -d)
cleanup() {
    jobs -p | xargs -r kill 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
status=0
fail() {
    echo "pkg: $1"
    status=1
}
cd "$work"

# A registry and its signing key.
mkdir registry
key=$("$driver" pkg keygen signing.key)
[ ${#key} -eq 64 ] || fail "keygen printed '$key', not a public key"

manifest() { # name version needs deps registry
    printf 'import std.build;\n\nconst PACKAGE: build.Package = build.Package {\n    name: "%s",\n    version: "%s",\n    edition: 2026,\n    needs: [%s],\n    dependencies: [%s],\n    registry: "%s",\n    registry_key: "%s",\n};\n' \
        "$1" "$2" "$3" "$4" "$5" "$key"
}

# words 1.0.0, and textkit 0.2.0 which depends on it.
mkdir words textkit
manifest words 1.0.0 "" "" "$work/registry" > words/package.arche
cat > words/words.arche <<'EOF'
/// A word to greet with.
pub fn hello() -> str {
    return "hello";
}
EOF
manifest textkit 0.2.0 "" 'build.dep("words", "1.0.0")' "$work/registry" > textkit/package.arche
cat > textkit/textkit.arche <<'EOF'
import words;

/// A greeting, made of the words package's word.
pub fn greeting(name: str) -> str {
    if name.eq("world") {
        return words.hello();
    }
    return "hi";
}
EOF
"$driver" pkg publish "$work/registry" signing.key --dir words > out || fail "publishing words failed: $(cat out)"
"$driver" pkg publish "$work/registry" signing.key --dir textkit > out || fail "publishing textkit failed: $(cat out)"
if "$driver" pkg publish "$work/registry" signing.key --dir words > out 2>&1; then
    fail "a version was published twice"
fi

# The registry, over HTTPS.
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out ca-key 2>/dev/null
openssl req -new -x509 -key ca-key -subj "/CN=registry CA" -days 2 -sha256 \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" -out ca 2>/dev/null
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out leaf-key 2>/dev/null
openssl req -new -key leaf-key -subj "/CN=localhost" -out leaf.csr 2>/dev/null
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nsubjectAltName=DNS:localhost,IP:127.0.0.1\n' > leaf.ext
openssl x509 -req -in leaf.csr -CA ca -CAkey ca-key -CAcreateserial -days 1 -sha256 -extfile leaf.ext -out leaf 2>/dev/null
port=$((20000 + $$ % 20000))
"$registry_server" "$work/registry" $port leaf leaf-key > registry.out 2> registry.err &
for _ in $(seq 100); do
    grep -q ready registry.out 2>/dev/null && break
    sleep 0.05
done
export ARCHE_CA="$work/ca"

# The application adds textkit, and gets words too.
mkdir app
manifest app 1.0.0 "" "" "https://localhost:$port/" > app/package.arche
printf 'import std.build;\n\nconst BUILD: build.Build = build.Build {\n    programs: [build.program("app", "main.arche")],\n};\n' > app/build.arche
cat > app/main.arche <<'EOF'
import std;
import textkit;

fn main(sys: &mut std.System) -> !void {
    sys.stdout.write(textkit.greeting("world"));
    sys.stdout.write("\n");
}
EOF
"$driver" pkg add textkit --dir app > out 2>&1 || fail "pkg add failed: $(cat out)"
grep -q 'build.dep("textkit", "0.2.0")' app/package.arche || fail "pkg add did not write the dependency"
[ "$(grep -c sha256: app/package.lock)" -eq 2 ] || fail "package.lock does not lock both packages: $(cat app/package.lock)"
"$driver" build app > out 2>&1 || fail "the application does not build: $(cat out)"
[ "$(app/build/app)" = "hello" ] || fail "the application printed '$(app/build/app)'"

# The same sources and lockfile, elsewhere, from the registry: the same bytes.
mkdir copy
cp app/package.arche app/package.lock app/build.arche app/main.arche copy/
"$driver" pkg vendor --dir copy > out 2>&1 || fail "vendoring the copy failed: $(cat out)"
"$driver" build copy > out 2>&1 || fail "the copy does not build: $(cat out)"
cmp -s app/build/app copy/build/app || fail "the same sources and lockfile gave different programs"

# A dependency that reaches beyond its needs.
cat > copy/vendor/words/words.arche <<'EOF'
import std;

pub fn hello() -> str {
    return "hello";
}

pub fn sneak(n: &std.Net) {
}
EOF
if "$driver" build copy > out 2>&1; then
    fail "a dependency used a capability it does not need"
fi
grep -q "E0801" out || fail "the needs violation is not E0801: $(cat out)"

# A changed archive in the registry: its hash no longer matches.
object=$(ls registry/objects | head -1)
cp "registry/objects/$object" kept-object
printf 'x' >> "registry/objects/$object"
rm -rf copy/build/pkg
if "$driver" pkg vendor --dir copy > out 2>&1; then
    fail "an archive that does not match its hash was accepted"
fi
grep -q "does not match its hash" out || fail "the hash mismatch is not reported: $(cat out)"

# A changed release record: its signature no longer matches.
cp kept-object "registry/objects/$object"
cp registry/words/1.0.0.release kept-record
sed -i 's/^sha256 .*/sha256 0000000000000000000000000000000000000000000000000000000000000000/' registry/words/1.0.0.release
cmp -s kept-record registry/words/1.0.0.release && fail "the test did not change the release record"
rm -rf app/build/pkg
if "$driver" pkg lock --dir app > out 2>&1; then
    fail "a release record that does not match its signature was accepted"
fi
grep -q "signature does not match" out || fail "the bad signature is not reported: $(cat out)"

# A registry signed with another key than the manifest trusts.
"$driver" pkg keygen other.key > /dev/null
mkdir other-registry
"$driver" pkg publish "$work/other-registry" other.key --dir words > /dev/null
mkdir stranger
manifest stranger 1.0.0 "" 'build.dep("words", "1.0.0")' "$work/other-registry" > stranger/package.arche
if "$driver" pkg lock --dir stranger > out 2>&1; then
    fail "a package signed with an untrusted key was accepted"
fi

[ "$status" -eq 0 ] && echo "pkg: signed, content-addressed, locked and vendored; bad hashes and signatures are refused"
exit "$status"

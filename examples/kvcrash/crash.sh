#!/usr/bin/env bash
# The KV store's crash test (docs/sprints.md S33): a writer that commits
# transaction after transaction is killed with SIGKILL after a random time,
# rounds times over one store; after each kill every transaction it said
# was committed must be there whole, and none in part. Usage: crash.sh
# <kvcrash binary> [rounds]
set -eu
bin=$1
rounds=${2:-30}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
db=$work/store
last=-1
for round in $(seq "$rounds"); do
    "$bin" write "$db" > "$work/out" &
    pid=$!
    # Between 20 and 400 milliseconds.
    sleep "0.$(printf '%03d' $((20 + RANDOM % 380)))"
    kill -9 "$pid"
    wait "$pid" 2>/dev/null || true
    committed=$(tail -n 1 "$work/out")
    # A line cut by the kill is not a whole number: the one before it is.
    case "$committed" in
        ''|*[!0-9]*) committed=$(tail -n 2 "$work/out" | head -n 1) ;;
    esac
    committed=${committed:-$last}
    if [ "$committed" -lt 0 ]; then
        continue
    fi
    result=$("$bin" check "$db" "$committed")
    case "$result" in
        ok*) ;;
        *) echo "round $round: $result"; exit 1 ;;
    esac
    last=$committed
done
echo "kv crash test: $rounds kills, every committed transaction whole, none in part ($result)"

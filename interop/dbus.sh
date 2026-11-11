#!/bin/sh
# lib/dbus against a real bus (docs/sprints.md S35), a dbus-daemon of its
# own in a temporary directory:
#   1. two Arche connections: names, a call and its answer, a signal, the
#      bus's list of names, an error answer (dbus_check echo);
#   2. dbus-send calling an Arche service (dbus_check serve);
#   3. the system bus's ModemManager asked for its modems, when there is
#      one; MODEM_MANAGER_REQUIRED=1 makes its absence a failure.
# Needs dbus-daemon and dbus-send. DBUS_BUS names another bus to try in
# dbus-daemon's place for 1 and 2: a command that serves one at the path it
# is given (arche-os's phone/dbusd).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
build=${WORK:-$here/../build/interop}
mkdir -p "$build"
work=$(mktemp -d)
bus=""
server=""
cleanup() {
    # The bus and the service, by their pids: a script's jobs are not
    # listed in its EXIT trap, and a bus left running holds the output.
    for p in $bus $server; do
        kill "$p" 2>/dev/null || true
    done
    rm -rf "$work"
}
trap cleanup EXIT
"$here/../compiler/arche-build" "$here/dbus_check.arche" "$build/dbus_check"
if [ -n "${DBUS_BUS:-}" ]; then
    $DBUS_BUS "$work/bus" > "$work/bus.log" 2>&1 &
else
    dbus-daemon --session --address="unix:path=$work/bus" --nofork &
fi
bus=$!
tries=0
while [ ! -S "$work/bus" ] && [ "$tries" -lt 50 ]; do
    sleep 0.1
    tries=$((tries + 1))
done
"$build/dbus_check" echo "$work/bus"
# dbus-send against the Arche service.
"$build/dbus_check" serve "$work/bus" > "$work/serve.out" &
server=$!
tries=0
while ! grep -q serving "$work/serve.out" 2>/dev/null && [ "$tries" -lt 50 ]; do
    sleep 0.1
    tries=$((tries + 1))
done
got=$(dbus-send --bus="unix:path=$work/bus" --print-reply --dest=org.arche.Test /org/arche/Test org.arche.Echo.Echo string:"from dbus-send" uint32:99)
case $got in
*'string "from dbus-send!"'*'uint32 100'*) echo "dbus: dbus-send's call answered by the Arche service ok" ;;
*)
    echo "dbus: dbus-send got: $got"
    exit 1
    ;;
esac
if dbus-send --bus="unix:path=$work/bus" --print-reply --dest=org.arche.Test /org/arche/Test org.arche.Echo.Nothing > "$work/err.out" 2>&1; then
    echo "dbus: an unknown method was answered"
    exit 1
fi
grep -q UnknownMethod "$work/err.out" && echo "dbus: an unknown method, UnknownMethod ok"
dbus-send --bus="unix:path=$work/bus" --print-reply --dest=org.arche.Test /org/arche/Test org.arche.Echo.Quit > /dev/null
wait %2
# ModemManager on the system bus.
if [ -S /run/dbus/system_bus_socket ] && dbus-send --system --print-reply --dest=org.freedesktop.DBus / org.freedesktop.DBus.NameHasOwner string:org.freedesktop.ModemManager1 2>/dev/null | grep -q 'boolean true'; then
    "$build/dbus_check" modems /run/dbus/system_bus_socket
elif [ "${MODEM_MANAGER_REQUIRED:-0}" = 1 ]; then
    echo "dbus: no ModemManager on the system bus, and MODEM_MANAGER_REQUIRED=1"
    exit 1
else
    echo "dbus: ModemManager skipped: it is not on the system bus"
fi
echo "dbus: the bus, dbus-send and ModemManager, as expected"

#!/bin/sh
# An Arche program as Linux's first process, in QEMU x86-64 (docs/sprints.md
# S35): the kernel of kernel.sh, an initramfs with init.arche as /init and
# DejaVu Sans, a virtio GPU of a phone's 720x1440, a virtio tablet and
# keyboard. init draws on the screen through DRM; the screen QEMU shows is
# held to the golden hash below. A touch and a key, sent through QEMU's
# monitor, come to init through evdev. LINUX_IMAGE names a kernel built
# already; KBUILD_TOOLS is kernel.sh's.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
build=${WORK:-$here/../../build/interop}
mkdir -p "$build"
work=$(mktemp -d)
cleanup() {
    jobs -p | xargs -r kill 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
kernel=${LINUX_IMAGE:-$("$here/kernel.sh")}
"$here/../../compiler/arche-build" "$here/init.arche" "$build/linux-init"
python3 -I "$here/cpio.py" "$work/initramfs" "init=$build/linux-init" "font.ttf=$here/../fonts/DejaVuSans.ttf"
qemu-system-x86_64 -cpu max -m 256M -kernel "$kernel" -initrd "$work/initramfs" \
    -append "console=ttyS0 rdinit=/init panic=-1 quiet" \
    -vga none -device virtio-gpu-pci,xres=720,yres=1440 -device virtio-tablet-pci -device virtio-keyboard-pci \
    -display none -serial stdio -qmp "unix:$work/qmp,server,nowait" -no-reboot > "$work/serial" 2>&1 &
qemu=$!
# Waits until the serial console says $1, for at most 30 seconds.
wait_for() {
    tries=0
    while ! grep -qF "$1" "$work/serial" 2>/dev/null; do
        tries=$((tries + 1))
        if [ "$tries" -gt 300 ] || ! kill -0 "$qemu" 2>/dev/null; then
            echo "linux: never said: $1"
            cat "$work/serial"
            exit 1
        fi
        sleep 0.1
    done
}
# A command to QEMU's QMP: its JSON, after the greeting and the capabilities.
qmp() {
    python3 -I - "$work/qmp" "$1" <<'PY'
import json, socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
f = s.makefile("rw")
f.readline()
for cmd in ['{"execute": "qmp_capabilities"}', sys.argv[2]]:
    f.write(cmd + "\n")
    f.flush()
    while True:
        r = json.loads(f.readline())
        if "return" in r or "error" in r:
            if "error" in r:
                sys.exit("qmp: " + json.dumps(r["error"]))
            break
s.close()
PY
}
wait_for "init: drew the text"
qmp '{"execute": "screendump", "arguments": {"filename": "'"$work/screen.ppm"'"}}'
wait_for "init: waiting for a touch"
# The middle of the screen: QEMU's absolute axes run from 0 to 32767.
qmp '{"execute": "input-send-event", "arguments": {"events": [{"type": "abs", "data": {"axis": "x", "value": 16384}}, {"type": "abs", "data": {"axis": "y", "value": 16384}}, {"type": "btn", "data": {"button": "left", "down": true}}]}}'
qmp '{"execute": "input-send-event", "arguments": {"events": [{"type": "btn", "data": {"button": "left", "down": false}}]}}'
wait_for "init: waiting for a key"
qmp '{"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": "a"}]}}'
wait_for "init: powering off"
wait "$qemu" || true
cp "$work/screen.ppm" "$build/linux-screen.ppm"
sed 's/^/    /' "$work/serial" | grep 'init:'
hash=$(sha256sum "$work/screen.ppm" | cut -c1-16)
want=e14bd889eaa3d466
fine=1
grep -q 'init: a screen of 720x1440' "$work/serial" || { echo "linux: not the 720x1440 screen"; fine=0; }
grep -q 'init: a touch at 360,720' "$work/serial" || { echo "linux: no touch at the middle of the screen"; fine=0; }
grep -q 'init: key 30' "$work/serial" || { echo "linux: the key a did not come"; fine=0; }
if [ "$hash" != "$want" ]; then
    echo "linux: the screen hashes $hash, not $want ($build/linux-screen.ppm)"
    fine=0
fi
[ "$fine" -eq 1 ] || exit 1
echo "linux: Arche's init drew on the screen through DRM and heard a touch and a key through evdev"

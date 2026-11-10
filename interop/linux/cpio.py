"""An initramfs in cpio's newc format, written here so that it holds a
/dev/console without root (interop/linux/run.sh): the kernel gives the
first process that console as its standard input and output.

usage: cpio.py <out> <dest>=<file>... ; each file's directories, /dev,
/proc, /sys, /tmp and /dev/console are there too."""
import sys

def entry(out, name, mode, data=b"", rdev=(0, 0), ino=[1]):
    ino[0] += 1
    name_b = name.encode() + b"\0"
    head = "070701" + "".join("%08X" % v for v in [
        ino[0], mode, 0, 0, 1, 0, len(data), 0, 0, rdev[0], rdev[1], len(name_b), 0])
    out.write(head.encode())
    out.write(name_b)
    out.write(b"\0" * ((4 - (110 + len(name_b)) % 4) % 4))
    out.write(data)
    out.write(b"\0" * ((4 - len(data) % 4) % 4))

def main():
    with open(sys.argv[1], "wb") as out:
        for d in ["dev", "proc", "sys", "tmp"]:
            entry(out, d, 0o040755)
        entry(out, "dev/console", 0o020600, rdev=(5, 1))
        made = {"dev", "proc", "sys", "tmp"}
        for spec in sys.argv[2:]:
            dest, src = spec.split("=", 1)
            # Its directories first, each once.
            parts = dest.split("/")[:-1]
            for i in range(1, len(parts) + 1):
                d = "/".join(parts[:i])
                if d not in made:
                    made.add(d)
                    entry(out, d, 0o040755)
            with open(src, "rb") as f:
                entry(out, dest, 0o100755, f.read())
        entry(out, "TRAILER!!!", 0)

main()

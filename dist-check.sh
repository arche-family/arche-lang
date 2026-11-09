#!/bin/sh
# A release on a clean machine (docs/sprints.md S34): the archive unpacked,
# a package with two modules built by arche-driver, with no C compiler and
# no LLVM, and run. CI runs it in a container with neither.
# Usage: dist-check.sh <arche-*.tar.gz>
set -eu
archive=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
if command -v clang > /dev/null 2>&1 || command -v cc > /dev/null 2>&1; then
    echo "dist-check: note: this machine has a C compiler; the build below does not use one"
fi
cd "$work"
tar xzf "$archive"
home=$(pwd)/$(ls -d arche-*)
mkdir hello
cat > hello/package.arche <<'END'
import std.build;

const PACKAGE: build.Package = build.Package {
    name: "hello",
    version: "1.0.0",
    edition: 2026,
    needs: [],
};
END
cat > hello/build.arche <<'END'
import std.build;

const BUILD: build.Build = build.Build {
    programs: [build.program("hello", "main.arche")],
};
END
cat > hello/greeting.arche <<'END'
import alloc;

pub fn greeting(name: str, a: alloc.Allocator) -> alloc.String {
    var s = alloc.String.from("hello, ", a);
    s.push_str(name);
    s.push_str(", from a release");
    return s;
}
END
cat > hello/main.arche <<'END'
import std;
import greeting;

fn main(sys: &mut std.System) -> !void {
    let g = greeting.greeting("world", sys.alloc);
    sys.stdout.write(g.view());
    sys.stdout.write("\n");
}
END
# Only the archive's tools, and no C compiler, whatever the machine has.
PATH=$home/bin:/usr/bin:/bin CLANG=no-such-clang ARCHE_HOME=$home arche-driver build hello
got=$(./hello/build/hello)
if [ "$got" != "hello, world, from a release" ]; then
    echo "dist-check: the program wrote '$got'"
    exit 1
fi
"$home/bin/arche" check hello/main.arche > /dev/null
echo "dist-check: $(basename "$archive") builds and runs a program on this machine with no C compiler"

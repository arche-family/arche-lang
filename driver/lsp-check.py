#!/usr/bin/env python3
"""A real session with arche-lsp (docs/sprints.md S36): a client over pipes, as
an editor is, with unsaved text in imported modules, every request, malformed
messages, and the time from didChange to diagnostics on the compiler's own
source. Usage: lsp-check.py <arche-lsp> <repository>"""
import json
import os
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time


LOGS = []


def fail(what):
    print("lsp-check: FAILED: " + what)
    for log in LOGS:
        log.flush()
        log.seek(0)
        said = log.read().decode(errors="replace")
        if said:
            print("the server's log:\n" + said[-4000:])
    sys.stdout.flush()
    os._exit(1)


class Client:
    def __init__(self, program, env=None):
        self.log = tempfile.TemporaryFile()
        LOGS.append(self.log)
        self.p = subprocess.Popen([program], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, env=env)
        self.buffer = b""
        self.next_id = 0
        self.notes = []
        # Windows cannot wait on a pipe with a timeout, so a thread reads it.
        self.incoming = queue.Queue()
        threading.Thread(target=self.pump, daemon=True).start()

    def pump(self):
        while True:
            try:
                data = os.read(self.p.stdout.fileno(), 65536)
            except OSError:
                data = b""
            self.incoming.put(data)
            if not data:
                return

    def send_raw(self, data):
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def frame(self, message):
        message["jsonrpc"] = "2.0"
        body = json.dumps(message, ensure_ascii=False).encode()
        return b"Content-Length: %d\r\n\r\n" % len(body) + body

    def send(self, message):
        self.send_raw(self.frame(message))

    def read_message(self, timeout):
        end = time.time() + timeout
        while True:
            head = self.buffer.find(b"\r\n\r\n")
            if head >= 0:
                length = None
                for line in self.buffer[:head].split(b"\r\n"):
                    if line.lower().startswith(b"content-length:"):
                        length = int(line.split(b":")[1])
                if length is None:
                    fail("a message with no length")
                start = head + 4
                if len(self.buffer) >= start + length:
                    body = self.buffer[start:start + length]
                    self.buffer = self.buffer[start + length:]
                    return json.loads(body)
            left = end - time.time()
            if left <= 0:
                fail("no message in %.0f s" % timeout)
            try:
                data = self.incoming.get(timeout=left)
            except queue.Empty:
                fail("no message in %.0f s" % timeout)
            if not data:
                fail("the server closed its output")
            self.buffer += data

    def read_response(self, timeout=10):
        """The next message that is not a notification."""
        while True:
            m = self.read_message(timeout)
            if "method" not in m:
                return m
            self.notes.append(m)

    def request(self, method, params, timeout=30):
        self.next_id += 1
        me = self.next_id
        self.send({"id": me, "method": method, "params": params})
        while True:
            m = self.read_message(timeout)
            if "method" in m:
                self.notes.append(m)
            elif m.get("id") == me:
                return m
            else:
                fail("an answer to something else: %r" % (m,))

    def notify(self, method, params):
        self.send({"method": method, "params": params})

    def diagnostics(self, uri, timeout=30, want_version=None):
        """The next diagnostics published for the file."""
        for i, m in enumerate(self.notes):
            if m.get("method") == "textDocument/publishDiagnostics" and m["params"]["uri"] == uri:
                if want_version is None or m["params"].get("version") == want_version:
                    del self.notes[i]
                    return m["params"]["diagnostics"]
        end = time.time() + timeout
        while True:
            m = self.read_message(end - time.time())
            if m.get("method") == "textDocument/publishDiagnostics" and m["params"]["uri"] == uri:
                if want_version is None or m["params"].get("version") == want_version:
                    return m["params"]["diagnostics"]
            elif "method" in m:
                self.notes.append(m)


def uri_of(path):
    return pathlib.Path(path).as_uri()


def write_text(path, text):
    # As it is: an Arche line ends with a newline on every system.
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def read_text(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def expect(what, got, want):
    if got != want:
        fail("%s:\n  got  %r\n  want %r" % (what, got, want))


def start(program, env=None):
    c = Client(program, env)
    r = c.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
    caps = r["result"]["capabilities"]
    for name in ["definitionProvider", "referencesProvider", "hoverProvider", "documentFormattingProvider"]:
        expect(name, caps.get(name), True)
    expect("sync", caps["textDocumentSync"]["change"], 1)
    expect("completion trigger", caps["completionProvider"]["triggerCharacters"], ["."])
    expect("code actions", caps["codeActionProvider"]["codeActionKinds"], ["quickfix"])
    expect("encoding", caps["positionEncoding"], "utf-16")
    c.notify("initialized", {})
    return c


def stop(c):
    expect("shutdown", c.request("shutdown", None)["result"], None)
    c.notify("exit", None)
    try:
        expect("status after shutdown", c.p.wait(10), 0)
    except subprocess.TimeoutExpired:
        fail("the server did not exit")


HELPER = """/// Doubles a number.
pub fn twice(n: i32) -> i32 {
    return n * 2;
}
"""

MAIN = """import helper;
import std;

/// Says ó.
fn greet(name: str) -> i32 {
    return helper.twice(1);
}

fn main(sys: &mut std.System) -> !void {
    let word = "ó𝐱"; let n = greet("x");
    _ = word;
    _ = n;
}
"""


def session(program, root):
    pkg = os.path.join(root, "pkg")
    os.makedirs(pkg)
    main_path = os.path.join(pkg, "main.arche")
    helper_path = os.path.join(pkg, "helper.arche")
    write_text(main_path, MAIN)
    write_text(helper_path, HELPER)
    write_text(os.path.join(pkg, "package.arche"), 'import std.build;\n\nconst PACKAGE: build.Package = build.Package {\n    name: "pkg",\n    version: "0.1.0",\n    edition: 2027,\n    needs: [],\n};\n')
    main_uri = uri_of(main_path)
    helper_uri = uri_of(helper_path)

    c = start(program)
    print("  initialize")

    # Unsaved text is for this user alone.
    work = os.listdir(os.environ["TMPDIR"])
    expect("one work directory", len(work), 1)
    work_dir = os.path.join(os.environ["TMPDIR"], work[0])
    if os.name == "posix":
        expect("its mode", oct(os.stat(work_dir).st_mode & 0o777), oct(0o700))
    else:
        # What it says after the directory's own name, which may hold "Users".
        rights = subprocess.run(["icacls", work_dir], capture_output=True).stdout.decode(errors="replace").upper().replace(work_dir.upper(), "")
        if "OWNER RIGHTS" not in rights or "USERS" in rights or "EVERYONE" in rights:
            fail("the work directory is open to others:\n" + rights)

    # A clean file has no diagnostics.
    c.notify("textDocument/didOpen", {"textDocument": {"uri": main_uri, "languageId": "arche", "version": 1, "text": MAIN}})
    expect("diagnostics of the clean file", c.diagnostics(main_uri, want_version=1), [])

    # An error in unsaved text appears and goes when it is fixed; the file on
    # the disk never had it.
    broken = MAIN.replace('return helper.twice(1);', 'return helper.twice(1) + missing;')
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 2}, "contentChanges": [{"text": broken}]})
    d = c.diagnostics(main_uri, want_version=2)
    if len(d) != 1:
        fail("one diagnostic for the unsaved error, got %r" % (d,))
    expect("its severity", d[0]["severity"], 1)
    expect("its source", d[0]["source"], "arche")
    expect("its code", d[0]["code"], "E0301")
    if "missing" not in d[0]["message"]:
        fail("the message names it: %r" % (d[0],))
    column = broken.split("\n")[5].index("missing")
    expect("its range", d[0]["range"], {"start": {"line": 5, "character": column}, "end": {"line": 5, "character": column + len("missing")}})
    print("  unsaved error appears")

    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 3}, "contentChanges": [{"text": MAIN}]})
    expect("diagnostics after the fix", c.diagnostics(main_uri, want_version=3), [])
    print("  and goes when fixed")

    # A burst of typing is one check, of the last text.
    burst = b""
    for v in range(4, 24):
        burst += c.frame({"method": "textDocument/didChange", "params": {"textDocument": {"uri": main_uri, "version": v}, "contentChanges": [{"text": MAIN}]}})
    c.send_raw(burst)
    expect("diagnostics after a burst", c.diagnostics(main_uri, want_version=23), [])
    stale = [m for m in c.notes if m.get("method") == "textDocument/publishDiagnostics"]
    expect("none for the text in between", stale, [])
    print("  a burst is checked once")

    # Unsaved text of an imported module counts, and an importer's errors
    # follow a change in it.
    c.notify("textDocument/didOpen", {"textDocument": {"uri": helper_uri, "languageId": "arche", "version": 1, "text": HELPER}})
    seen = {}
    while main_uri not in seen or helper_uri not in seen:
        m = c.read_message(30)
        if m.get("method") == "textDocument/publishDiagnostics":
            seen[m["params"]["uri"]] = m["params"]["diagnostics"]
    expect("both are clean", seen, {main_uri: [], helper_uri: []})
    renamed = HELPER.replace("twice", "double")
    c.notify("textDocument/didChange", {"textDocument": {"uri": helper_uri, "version": 2}, "contentChanges": [{"text": renamed}]})
    d = c.diagnostics(main_uri)
    if not any("twice" in x["message"] for x in d):
        fail("the importer sees the module's unsaved text: %r" % (d,))
    print("  a module's unsaved text reaches its importer")
    c.notify("textDocument/didChange", {"textDocument": {"uri": helper_uri, "version": 3}, "contentChanges": [{"text": HELPER}]})
    expect("clean again", c.diagnostics(main_uri), [])
    c.notes.clear()

    # definition: across files, from the place in unsaved text. The word is
    # moved down a line from the saved file's place, so an answer from the
    # disk would be wrong.
    moved = MAIN.replace("import std;\n", "import std;\n\n")
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 30}, "contentChanges": [{"text": moved}]})
    expect("moved text is clean", c.diagnostics(main_uri, want_version=30), [])
    lines = moved.split("\n")
    call = next(i for i, l in enumerate(lines) if "helper.twice" in l)
    at = lines[call].index("twice") + 1
    r = c.request("textDocument/definition", {"textDocument": {"uri": main_uri}, "position": {"line": call, "character": at}})
    expect("definition", r["result"], [{"uri": helper_uri, "range": {"start": {"line": 1, "character": 7}, "end": {"line": 1, "character": 12}}}])
    print("  definition")

    # references: with and without the declaration.
    greet_line = next(i for i, l in enumerate(lines) if "fn greet" in l)
    greet_col = lines[greet_line].index("greet")
    use_line = next(i for i, l in enumerate(lines) if "greet(\"x\")" in l)
    # UTF-16: "ó" is 1 unit (2 bytes), "𝐱" is 2 units (4 bytes).
    use_col = lines[use_line].encode("utf-16-le").decode("utf-16-le").index("greet")
    use_units = len(lines[use_line][:use_col].encode("utf-16-le")) // 2
    declared = {"uri": main_uri, "range": {"start": {"line": greet_line, "character": greet_col}, "end": {"line": greet_line, "character": greet_col + 5}}}
    used = {"uri": main_uri, "range": {"start": {"line": use_line, "character": use_units}, "end": {"line": use_line, "character": use_units + 5}}}
    position = {"line": greet_line, "character": greet_col + 1}
    r = c.request("textDocument/references", {"textDocument": {"uri": main_uri}, "position": position, "context": {"includeDeclaration": True}})
    expect("references with the declaration", r["result"], [declared, used])
    r = c.request("textDocument/references", {"textDocument": {"uri": main_uri}, "position": position, "context": {"includeDeclaration": False}})
    expect("references without it", r["result"], [used])
    # The same, asked after the non-ASCII text: UTF-16 columns come in.
    r = c.request("textDocument/definition", {"textDocument": {"uri": main_uri}, "position": {"line": use_line, "character": use_units + 2}})
    expect("definition past non-ASCII text", r["result"], [declared])
    print("  references, and UTF-16 columns")

    # hover
    r = c.request("textDocument/hover", {"textDocument": {"uri": main_uri}, "position": {"line": call, "character": at}})
    expect("hover kind", r["result"]["contents"]["kind"], "markdown")
    expect("hover", r["result"]["contents"]["value"], "```arche\npub fn twice(n: i32) -> i32\n```\n\nDoubles a number.")
    r = c.request("textDocument/hover", {"textDocument": {"uri": main_uri}, "position": {"line": 0, "character": 0}})
    if r["result"] is not None and "```arche" not in r["result"]["contents"]["value"]:
        fail("hover on an import: %r" % (r,))
    print("  hover")

    # completion: members in text that does not parse yet, with kinds.
    typing = moved.replace("return helper.twice(1);", "return helper.tw")
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 31}, "contentChanges": [{"text": typing}]})
    tl = typing.split("\n")
    line = next(i for i, l in enumerate(tl) if "helper.tw" in l)
    r = c.request("textDocument/completion", {"textDocument": {"uri": main_uri}, "position": {"line": line, "character": len(tl[line])}})
    expect("incomplete", r["result"]["isIncomplete"], False)
    labels = {i["label"]: i for i in r["result"]["items"]}
    if "twice" not in labels:
        fail("completion of a module's function: %r" % (r,))
    expect("its kind", labels["twice"]["kind"], 3)
    expect("its detail", labels["twice"]["detail"], "pub fn twice(n: i32) -> i32")
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 32}, "contentChanges": [{"text": moved}]})
    expect("clean after typing", c.diagnostics(main_uri, want_version=32), [])
    c.notes.clear()
    print("  completion")

    # formatting: one edit that gives the formatted text.
    ugly = moved.replace("    return helper.twice(1);", "  return   helper.twice( 1 ) ;")
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 33}, "contentChanges": [{"text": ugly}]})
    expect("ugly is clean", c.diagnostics(main_uri, want_version=33), [])
    r = c.request("textDocument/formatting", {"textDocument": {"uri": main_uri}, "options": {"tabSize": 4, "insertSpaces": True}})
    if len(r["result"]) != 1:
        fail("formatting: %r" % (r,))
    edit = r["result"][0]
    expect("edit starts at the top", edit["range"]["start"], {"line": 0, "character": 0})
    expect("the edit reaches the end", edit["range"]["end"], {"line": len(ugly.split("\n")) - 1, "character": 0})
    if "    return helper.twice(1);\n" not in edit["newText"]:
        fail("formatted text: %r" % (edit["newText"],))
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 330}, "contentChanges": [{"text": edit["newText"]}]})
    r = c.request("textDocument/formatting", {"textDocument": {"uri": main_uri}, "options": {"tabSize": 4, "insertSpaces": True}})
    expect("formatted text needs no edit", r["result"], [])
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 34}, "contentChanges": [{"text": moved}]})
    expect("clean after formatting", c.diagnostics(main_uri, want_version=34), [])
    c.notes.clear()
    expect("the file on the disk is untouched", read_text(main_path), MAIN)
    print("  formatting")

    # A fix: a compiler's suggestion, as a quick fix.
    fixable = moved + "fn pair(left: i32, right: i32) -> i32 {\n    return left + right;\n}\n\nfn call_pair() -> i32 {\n    return pair(1, 2);\n}\n"
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 35}, "contentChanges": [{"text": fixable}]})
    d = c.diagnostics(main_uri, want_version=35)
    if not d:
        fail("the fixable text has no diagnostic")
    l = d[0]["range"]["start"]["line"]
    r = c.request("textDocument/codeAction", {"textDocument": {"uri": main_uri}, "range": {"start": {"line": l, "character": 0}, "end": {"line": l, "character": 0}}, "context": {"diagnostics": d}})
    if not r["result"]:
        fail("no quick fix for %r: %r" % (d, r))
    edits = []
    for action in r["result"]:
        expect("kind", action["kind"], "quickfix")
        edits += action["edit"]["changes"][main_uri]
    text_lines = fixable.split("\n")
    for e in sorted(edits, key=lambda x: (x["range"]["start"]["line"], x["range"]["start"]["character"]), reverse=True):
        s, f = e["range"]["start"], e["range"]["end"]
        if s["line"] != f["line"]:
            fail("a fix over lines: %r" % (e,))
        t = text_lines[s["line"]]
        text_lines[s["line"]] = t[:s["character"]] + e["newText"] + t[f["character"]:]
    applied = "\n".join(text_lines)
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 36}, "contentChanges": [{"text": applied}]})
    expect("the fix removes the error", c.diagnostics(main_uri, want_version=36), [])
    c.notes.clear()
    print("  quick fix")

    # Closing clears the file's diagnostics, and the next request has no text
    # to go by.
    c.notify("textDocument/didChange", {"textDocument": {"uri": main_uri, "version": 37}, "contentChanges": [{"text": broken}]})
    if not c.diagnostics(main_uri, want_version=37):
        fail("no error before closing")
    c.notify("textDocument/didClose", {"textDocument": {"uri": main_uri}})
    expect("closed", c.diagnostics(main_uri), [])
    r = c.request("textDocument/hover", {"textDocument": {"uri": main_uri}, "position": {"line": 0, "character": 0}})
    expect("hover in a closed file", r["result"], None)
    c.notes.clear()
    print("  didClose")

    # Errors in a module that is not open are published for it, and cleared.
    c.notify("textDocument/didClose", {"textDocument": {"uri": helper_uri}})
    c.notify("textDocument/didOpen", {"textDocument": {"uri": main_uri, "languageId": "arche", "version": 40, "text": MAIN}})
    expect("reopened", c.diagnostics(main_uri, want_version=40), [])
    c.notes.clear()
    write_text(helper_path, "pub fn twice(n: i32) -> i32 {\n    return nothing;\n}\n")
    c.notify("textDocument/didSave", {"textDocument": {"uri": main_uri}})
    d = c.diagnostics(helper_uri)
    if not d or "nothing" not in d[0]["message"]:
        fail("errors in an imported file that is not open: %r" % (d,))
    write_text(helper_path, HELPER)
    c.notify("textDocument/didSave", {"textDocument": {"uri": main_uri}})
    expect("cleared when gone", c.diagnostics(helper_uri), [])
    c.notes.clear()
    print("  errors in files that are not open")

    # A file with a space in its name, and one that is no file at all.
    odd_path = os.path.join(pkg, "with space ó.arche")
    # Lower-case hex: the answer comes under the client's own spelling.
    odd_uri = uri_of(odd_path).replace("%C3%B3", "%c3%b3")
    if os.name != "posix":
        # An editor may write a drive this way too: file:///c%3A/a.
        drive = odd_path[0]
        odd_uri = odd_uri.replace("file:///" + drive + ":", "file:///" + drive.lower() + "%3A")
    c.notify("textDocument/didOpen", {"textDocument": {"uri": odd_uri, "languageId": "arche", "version": 1, "text": "fn nothing_here() {\n    return nope;\n}\n"}})
    d = c.diagnostics(odd_uri)
    if not any("nope" in x["message"] for x in d):
        fail("a file name to encode: %r" % (d,))
    c.notify("textDocument/didClose", {"textDocument": {"uri": odd_uri}})
    expect("its close", c.diagnostics(odd_uri), [])
    c.notes.clear()
    c.notify("textDocument/didOpen", {"textDocument": {"uri": "untitled:Untitled-1", "languageId": "arche", "version": 1, "text": "x"}})
    r = c.request("textDocument/hover", {"textDocument": {"uri": "untitled:Untitled-1"}, "position": {"line": 0, "character": 0}})
    expect("a URI that is no file", r["result"], None)
    print("  URIs")

    # What a client gets wrong.
    r = c.request("no/such/method", {})
    expect("unknown request", r["error"]["code"], -32601)
    c.notify("no/such/notification", {})
    c.send_raw(b"Content-Length: 9\r\n\r\n{not json")
    m = c.read_response()
    expect("not JSON", (m["error"]["code"], m["id"]), (-32700, None))
    c.send_raw(b"Content-Length: 3\r\n\r\n\xff\xfe\xfd")
    m = c.read_response()
    expect("not UTF-8", m["error"]["code"], -32700)
    c.send({"id": 77})
    r = c.request("textDocument/definition", {"position": 5})
    expect("a request with no document", r["result"], None)
    r = c.request("textDocument/hover", {"textDocument": {"uri": 12}, "position": "x"})
    expect("fields of the wrong type", r["result"], None)
    r = c.request("textDocument/hover", {"textDocument": {"uri": main_uri}, "position": {"line": 100000, "character": 100000}})
    if "error" in r:
        fail("a position past the end: %r" % (r,))
    # Case in header names does not matter; other headers are allowed.
    body = b'{"jsonrpc":"2.0","id":"text id","method":"no/such"}'
    c.send_raw(b"content-TYPE: application/vscode-jsonrpc; charset=utf-8\r\ncontent-length: %d\r\n\r\n" % len(body) + body)
    m = c.read_response()
    expect("a string id", (m["id"], m["error"]["code"]), ("text id", -32601))
    # Ids come back as they came.
    for raw, want in [(b"-3", -3), (b"1.5", 1.5), (b"0", 0), (b'""', "")]:
        body = b'{"jsonrpc":"2.0","id":' + raw + b',"method":"no/such"}'
        c.send_raw(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        m = c.read_response()
        expect("the id %r" % (raw,), (m["id"], m["error"]["code"]), (want, -32601))
    # A message in two pieces, then two in one piece.
    one = b'{"jsonrpc":"2.0","id":501,"method":"no/such"}'
    two = b'{"jsonrpc":"2.0","id":502,"method":"no/such"}'
    frame = b"Content-Length: %d\r\n\r\n" % len(one) + one
    c.send_raw(frame[:10])
    time.sleep(0.05)
    c.send_raw(frame[10:] + b"Content-Length: %d\r\n\r\n" % len(two) + two)
    expect("first of two", c.read_response()["id"], 501)
    expect("second of two", c.read_response()["id"], 502)
    print("  malformed messages")

    stop(c)
    print("  shutdown and exit")

    # Before initialize, and an exit without a shutdown.
    c = Client(program)
    c.send({"id": 1, "method": "textDocument/hover", "params": {}})
    expect("before initialize", c.read_message(10)["error"]["code"], -32002)
    c.send({"id": 2, "method": "initialize", "params": {"capabilities": {}}})
    expect("initialize", c.read_message(10)["id"], 2)
    c.notify("exit", None)
    expect("status without a shutdown", c.p.wait(10), 1)

    # The protocol broken: the server says so and ends, leaving nothing.
    c = Client(program)
    c.send_raw(b"this is not a header\r\n\r\n")
    expect("status on a broken stream", c.p.wait(10), 1)

    # The client going away.
    c = Client(program)
    c.p.stdin.close()
    expect("status when the input ends", c.p.wait(10), 1)
    print("  how a session ends")


def client_gone(program):
    """A client that stops reading: the server notices at the end of its input,
    and tidies up, however much it had to write by then."""
    p = subprocess.Popen([program], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    p.stdout.close()
    body = b'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
    frame = b"Content-Length: %d\r\n\r\n" % len(body) + body
    try:
        for _ in range(3):
            p.stdin.write(frame)
            p.stdin.flush()
            time.sleep(0.1)
    except OSError:
        fail("the server died writing to a client that went away")
    p.stdin.close()
    expect("status when the client went away", p.wait(10), 1)
    print("  a client that goes away")


def many_errors(program, root):
    """A file full of errors: all of them come, and soon."""
    path = os.path.join(root, "many.arche")
    count = 3000
    text = "fn main2() -> i32 {\n" + "".join("    let x%d: i32 = missing%d;\n" % (i, i) for i in range(count)) + "    return 0;\n}\n"
    c = start(program)
    began = time.time()
    c.notify("textDocument/didOpen", {"textDocument": {"uri": uri_of(path), "languageId": "arche", "version": 1, "text": text}})
    d = c.diagnostics(uri_of(path), want_version=1)
    took = time.time() - began
    errors = [x for x in d if "missing" in x["message"]]
    expect("every error", len(errors), count)
    stop(c)
    print("  %d errors published in %.0f ms" % (count, took * 1000))
    if took >= 10.0:
        fail("a file full of errors took too long")


def broken_compiler(program, root):
    """A compiler that is missing, crashes or talks nonsense: the server stays."""
    crash = os.path.join(root, "crash.sh")
    # Windows starts neither script, which is one more way to be broken.
    write_text(crash, "#!/bin/sh\necho panicked >&2\nexit 101\n")
    nonsense = os.path.join(root, "nonsense.sh")
    write_text(nonsense, "#!/bin/sh\necho 'this is { not JSON'\nexit 1\n")
    os.chmod(crash, 0o755)
    os.chmod(nonsense, 0o755)
    path = os.path.join(root, "alone.arche")
    for compiler in [os.path.join(root, "no-such-compiler"), crash, nonsense]:
        env = dict(os.environ)
        env["ARCHE"] = compiler
        c = start(program, env)
        c.notify("textDocument/didOpen", {"textDocument": {"uri": uri_of(path), "languageId": "arche", "version": 1, "text": "fn f() {}\n"}})
        at = {"textDocument": {"uri": uri_of(path)}, "position": {"line": 0, "character": 3}}
        expect("hover", c.request("textDocument/hover", at)["result"], None)
        expect("definition", c.request("textDocument/definition", at)["result"], None)
        expect("completion", c.request("textDocument/completion", at)["result"]["items"], [])
        expect("formatting", c.request("textDocument/formatting", {"textDocument": at["textDocument"], "options": {}})["result"], None)
        stop(c)
    print("  a broken compiler does not take the server down")


def responsive(program, repo):
    """S36's measure: from didChange to diagnostics on the compiler's source."""
    path = os.path.join(repo, "compiler", "main.arche")
    text = read_text(path)
    uri = uri_of(path)
    c = start(program)
    c.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "arche", "version": 1, "text": text}})
    d = c.diagnostics(uri, want_version=1)
    expect("the compiler's source is clean", d, [])
    worst = 0.0
    broken = text + "\nfn broken_on_purpose() -> i32 {\n    return nothing_at_all;\n}\n"
    version = 2
    for i in range(4):
        now = broken if i % 2 == 0 else text
        began = time.time()
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": now}]})
        d = c.diagnostics(uri, want_version=version)
        took = time.time() - began
        worst = max(worst, took)
        expect("diagnostics of round %d" % i, len(d), 1 if i % 2 == 0 else 0)
        version += 1
    stop(c)
    print("  didChange to diagnostics on compiler/main.arche: %.0f ms at worst" % (worst * 1000))
    if worst >= 1.0:
        fail("over a second")


def main():
    program = os.path.abspath(sys.argv[1])
    repo = os.path.abspath(sys.argv[2])
    root = tempfile.mkdtemp(prefix="arche-lsp-check-")
    env_tmp = os.path.join(root, "tmp")
    os.makedirs(env_tmp)
    for name in ["TMPDIR", "TEMP", "TMP"]:
        os.environ[name] = env_tmp
    try:
        session(program, os.path.realpath(root))
        client_gone(program)
        many_errors(program, os.path.realpath(root))
        broken_compiler(program, os.path.realpath(root))
        responsive(program, repo)
        left = os.listdir(env_tmp)
        expect("work directories left behind", left, [])
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("lsp-check: passed")


main()

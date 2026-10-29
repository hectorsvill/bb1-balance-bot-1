"""todo.py - BB1's todo list, read from (and ticked in) the Obsidian vault. JSON out, for AI agents.

Usage:
    tools/venv/bin/python tools/todo.py list                   # every checkbox, grouped by roadmap section
    tools/venv/bin/python tools/todo.py list --open            # only the open ones
    tools/venv/bin/python tools/todo.py done 3f2a91c0          # tick one box (id from 'list')
    tools/venv/bin/python tools/todo.py undo 3f2a91c0          # untick it again
    tools/venv/bin/python tools/todo.py add --section "Hardware to fit" "Mount the OLED"
    tools/venv/bin/python tools/todo.py status                 # firmware, runs, branches, PRs, CI
    tools/venv/bin/python tools/todo.py serve                  # the same on a web page: http://localhost:8091

The todos are not stored here. The vault is the one place they live:
    BB1 - Roadmap.md     milestone checklists ("- [ ] ..." / "- [x] ...") -> the todos
    BB1 - Dashboard.md   the Now / Next / Later bullets -> shown as "focus", read only
Ticking a box rewrites only that one line of the Markdown file, and only if the
line still reads exactly what we saw: if someone (or Obsidian) changed it in the
meantime we refuse instead of guessing. Obsidian Git commits the vault by
itself, so this tool never runs git there.

A todo's id is a short hash of its section and text, so it stays the same when
the box is ticked or other lines move; it changes if the text is edited.

Exit codes: 0 ok, 1 refused or failed ({"ok": false, "error": ...}), 2 bad usage.
"""

import argparse
import csv
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
import tempfile
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PAGE = Path(__file__).resolve().parent / "todo_page.html"
DEFAULT_VAULT = Path.home() / "Documents/GitHub/hectorsvill_brain/Projects/bb1 - balance bot 1"
ROADMAP = "BB1 - Roadmap.md"
DASHBOARD = "BB1 - Dashboard.md"
GH_TIMEOUT_S = 10            # gh talks to GitHub; never let a slow network hang the page
WRITE_LOCK = threading.Lock()  # the page server answers in threads; edits to the file go one by one

# "- [ ] text" or "  * [x] text": indent, bullet, box, text. Group 3 is the box's one character.
CHECKBOX = re.compile(r"^(\s*)([-*+]) \[([ xX])\] (.*?)\s*$")
HEADING = re.compile(r"^(#{1,6}) (.*?)\s*$")


class Refused(Exception):
    """Something we won't do (unknown id, the line changed, no such section)."""


# --- reading the vault ---------------------------------------------------------------------

def read_lines(path):
    """The file as a list of lines with their own line endings kept, so writing it back
    changes nothing but the one line we mean to change."""
    if not path.exists():
        raise Refused(f"no such file: {path} (set --vault)")
    return raw_text(path).splitlines(keepends=True)


def raw_text(path):
    # newline="": keep "\r\n" as it is (plain read_text would quietly turn it into "\n").
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def todo_id(section, text, nth):
    """Same section + same text = same id, ticked or not. nth tells twins apart."""
    return hashlib.sha1(f"{section}\n{text}\n{nth}".encode()).hexdigest()[:8]


def parse_checklist(lines, file_name):
    """Every checkbox line, grouped under the '## ' heading above it.
    Returns sections in file order: [{"title", "items": [{"id", "done", "text", "line", "raw"}]}]."""
    sections, current, seen = [], None, {}
    for n, raw in enumerate(lines, start=1):
        line = raw.rstrip("\r\n")
        h = HEADING.match(line)
        if h and len(h.group(1)) == 2:              # '## Milestone 3 - Driving' starts a section
            current = {"title": h.group(2), "file": file_name, "line": n, "items": []}
            sections.append(current)
            continue
        m = CHECKBOX.match(line)
        if not m:
            continue
        if current is None:                         # boxes above the first '##' still count
            current = {"title": "(top)", "file": file_name, "line": 1, "items": []}
            sections.append(current)
        text = m.group(4)
        key = (current["title"], text)
        seen[key] = seen.get(key, 0) + 1
        current["items"].append({"id": todo_id(current["title"], text, seen[key]),
                                 "done": m.group(3) != " ", "text": text,
                                 "indent": len(m.group(1)), "line": n, "raw": line})
    for s in sections:
        s["done"] = sum(i["done"] for i in s["items"])
        s["total"] = len(s["items"])
    return [s for s in sections if s["items"]]


def parse_focus(lines):
    """The Dashboard's '## Now / Next / Later' block: '**Now**' then '- bullets', and so on.
    Also the 'Current Phase' paragraph. Read only: these are plain bullets, not boxes."""
    focus, phase, block, group = {}, [], None, None
    for raw in lines:
        line = raw.rstrip("\r\n")
        h = HEADING.match(line)
        if h:
            block = h.group(2).lower()
            group = None
            continue
        if block == "current phase":
            if line.strip() and not line.startswith(">"):   # the first paragraph, not the callouts
                phase.append(line.strip())
            elif phase:
                block = None                                 # paragraph over
        elif block and block.startswith("now"):
            g = re.match(r"^\*\*(.+?)\*\*\s*$", line.strip())
            if g:
                group = g.group(1)
                focus[group] = []
            elif group and re.match(r"^[-*+] ", line):
                focus[group].append(line[2:].strip())
    return {"phase": " ".join(phase), "groups": focus}


def load(vault):
    """Everything the page and 'list' show, read fresh from the files every time."""
    vault = Path(vault)
    sections = parse_checklist(read_lines(vault / ROADMAP), ROADMAP)
    try:
        focus = parse_focus(read_lines(vault / DASHBOARD))
    except Refused as e:                            # the Dashboard is optional: fail soft
        focus = {"phase": "", "groups": {}, "error": str(e)}
    done = sum(s["done"] for s in sections)
    total = sum(s["total"] for s in sections)
    return {"ok": True, "vault": str(vault), "done": done, "total": total,
            "sections": sections, "focus": focus}


def find(sections, tid):
    for s in sections:
        for item in s["items"]:
            if item["id"] == tid:
                return s, item
    raise Refused(f"no todo with id {tid!r}: run 'list' again (ids change when the text is edited)")


# --- writing one line back, safely ---------------------------------------------------------

def write_lines(path, lines, before):
    """Write the file back only if nobody changed it since we read it ('before' is the text
    we read). Temp file + rename, so a crash can't leave half a roadmap behind."""
    if raw_text(path) != before:
        raise Refused(f"{path.name} changed while we were editing it: reload and try again")
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".todo-", suffix=".md")
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as f:
            f.write("".join(lines))
        os.chmod(tmp, path.stat().st_mode & 0o777)  # mkstemp makes it owner-only; keep the old mode
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def set_done(vault, tid, done, expect=None):
    """Tick (done=True) or untick one box. 'expect' is the exact line the caller saw (the page
    sends it): if the file no longer has that line where it was, we refuse."""
    path = Path(vault) / ROADMAP
    lines = read_lines(path)
    before = "".join(lines)
    section, item = find(parse_checklist(lines, ROADMAP), tid)
    if expect is not None and expect != item["raw"]:
        raise Refused("that line changed in the file since the page loaded it: reload")
    if item["done"] == done:
        return {"ok": True, "id": tid, "done": done, "changed": False, "text": item["text"]}
    old = lines[item["line"] - 1]
    m = CHECKBOX.match(old.rstrip("\r\n"))
    box_at = old.index("[", len(m.group(1)))        # the '[' right after the bullet
    new = old[:box_at] + ("[x]" if done else "[ ]") + old[box_at + 3:]
    lines[item["line"] - 1] = new
    write_lines(path, lines, before)
    return {"ok": True, "id": tid, "done": done, "changed": True, "text": item["text"],
            "section": section["title"], "line": item["line"]}


def add(vault, section_title, text):
    """Add '- [ ] text' at the end of a section's checklist (after its last box)."""
    text = " ".join(str(text).split())              # one line, no stray spaces or newlines
    if not text:
        raise Refused("empty todo")
    path = Path(vault) / ROADMAP
    lines = read_lines(path)
    before = "".join(lines)
    sections = parse_checklist(lines, ROADMAP)
    matches = [s for s in sections if s["title"] == section_title] or \
              [s for s in sections if section_title.lower() in s["title"].lower()]
    if len(matches) != 1:
        names = ", ".join(repr(s["title"]) for s in sections)
        raise Refused(f"{len(matches)} sections match {section_title!r}; pick one of {names}")
    section = matches[0]
    last = section["items"][-1]                     # insert right after the section's last box
    end = lines[last["line"] - 1]
    eol = "\r\n" if lines[0].endswith("\r\n") else "\n"      # match the file's own line endings
    if not end.endswith("\n"):                      # the file's last line had no newline
        lines[last["line"] - 1] = end + eol
    lines.insert(last["line"], f"- [ ] {text}{eol}")
    write_lines(path, lines, before)
    twins = sum(1 for i in section["items"] if i["text"] == text) + 1     # this one is the newest twin
    return {"ok": True, "id": todo_id(section["title"], text, twins), "section": section["title"],
            "text": text, "line": last["line"] + 1}


# --- the status strip: live facts, each one allowed to fail on its own ---------------------

def soft(fn):
    """Run one fact; a missing file or a broken gh becomes {"ok": false} for that fact only."""
    try:
        return {"ok": True, **fn()}
    except Exception as e:                          # noqa: BLE001 - every fact fails soft by design
        return {"ok": False, "error": f"{type(e).__name__}: {e}"}


def run(cmd, cwd=REPO):
    out = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=GH_TIMEOUT_S)
    if out.returncode != 0:
        said = (out.stderr or out.stdout).strip().splitlines()
        raise RuntimeError(said[-1] if said else f"{cmd[0]} exit {out.returncode}")
    return out.stdout


def firmware(logs):
    with open(Path(logs) / "flash_history.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise RuntimeError("flash_history.csv is empty")
    return {"last_flash": rows[-1], "flashes": len(rows)}


def runs(logs):
    db_path = Path(logs) / "runs.sqlite"
    if not db_path.exists():
        raise FileNotFoundError(f"{db_path} (run tools/runs_db.py build)")
    db = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)    # read only: we only count
    try:
        count, armed_s, last = db.execute("SELECT count(*), sum(armed_s), max(started) FROM runs").fetchone()
    finally:
        db.close()
    return {"runs": count, "armed_min": round((armed_s or 0) / 60, 1), "last_run": last}


def git():
    branch = run(["git", "branch", "--show-current"]).strip()
    branches = run(["git", "branch", "--format=%(refname:short)"]).split()
    return {"branch": branch, "branches": branches}


def prs():
    return {"prs": json.loads(run(["gh", "pr", "list", "--json", "number,title,headRefName,isDraft,url"]))}


def ci():
    return {"runs": json.loads(run(["gh", "run", "list", "--limit", "3", "--json",
                                    "displayTitle,headBranch,status,conclusion,createdAt,url"]))}


def status(logs):
    """Never contacts the robot: only files on this Mac, git, and GitHub through gh."""
    return {"ok": True, "firmware": soft(lambda: firmware(logs)), "runs": soft(lambda: runs(logs)),
            "git": soft(git), "prs": soft(prs), "ci": soft(ci)}


# --- the web page: the same answers over HTTP, on this computer only -------------------------

class Handler(BaseHTTPRequestHandler):
    vault = DEFAULT_VAULT
    logs = REPO / "logs"

    def send(self, code, body, kind="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def answer(self, fn):
        try:
            self.send(200, json.dumps(fn()))
        except Refused as e:
            self.send(400, json.dumps({"ok": False, "error": str(e)}))

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/":
            return self.send(200, PAGE.read_text(), "text/html; charset=utf-8")
        if path == "/api/todos":
            return self.answer(lambda: load(self.vault))
        if path == "/api/status":
            return self.answer(lambda: status(self.logs))
        self.send(404, json.dumps({"ok": False, "error": f"no route {path}"}))

    def do_POST(self):
        # Only JSON bodies: a browser won't send a cross-site JSON POST without asking first
        # (CORS preflight), which we never allow, so another web page can't tick boxes for us.
        if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
            return self.send(415, json.dumps({"ok": False, "error": "send Content-Type: application/json"}))
        try:
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        except ValueError:
            return self.send(400, json.dumps({"ok": False, "error": "body is not JSON"}))
        path = urllib.parse.urlparse(self.path).path
        with WRITE_LOCK:                            # two quick clicks: one edit at a time
            if path in ("/api/done", "/api/undo"):
                return self.answer(lambda: set_done(self.vault, str(body.get("id", "")), path == "/api/done",
                                                    body.get("line")))
            if path == "/api/add":
                return self.answer(lambda: add(self.vault, str(body.get("section", "")), body.get("text", "")))
        self.send(404, json.dumps({"ok": False, "error": f"no route {path}"}))

    def log_message(self, *args):                   # quiet: one line per request is noise here
        pass


def serve(vault, logs, port):
    Handler.vault, Handler.logs = Path(vault), Path(logs)
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)      # this computer only
    print(f"Todo page: http://localhost:{port}   (JSON: GET /api/todos, /api/status; "
          f"POST /api/done, /api/undo, /api/add)  Vault: {vault}  Ctrl-C stops.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vault", default=os.environ.get("BB1_VAULT", str(DEFAULT_VAULT)),
                    help="folder with BB1 - Roadmap.md and BB1 - Dashboard.md (default $BB1_VAULT or the brain)")
    ap.add_argument("--logs", default=str(REPO / "logs"), help="folder with runs.sqlite and flash_history.csv")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list", help="every todo, grouped by section, open ones first")
    p.add_argument("--open", action="store_true", help="only open todos")
    p.add_argument("--section", help="only sections whose title contains this")
    for name in ("done", "undo"):
        p = sub.add_parser(name, help="tick one box" if name == "done" else "untick one box")
        p.add_argument("id")
    p = sub.add_parser("add", help="add an open todo at the end of a section")
    p.add_argument("--section", required=True, help="section title (or a unique part of it)")
    p.add_argument("text")
    sub.add_parser("status", help="firmware, runs, branches, PRs, CI (never the robot)")
    p = sub.add_parser("serve", help="the web page")
    p.add_argument("--port", type=int, default=8091)
    args = ap.parse_args()                          # bad usage exits 2 here

    try:
        if args.cmd == "serve":
            return serve(args.vault, args.logs, args.port)
        if args.cmd == "list":
            answer = load(args.vault)
            for s in answer["sections"]:
                if args.open:
                    s["items"] = [i for i in s["items"] if not i["done"]]
                s["items"].sort(key=lambda i: i["done"])            # open first, file order kept
            answer["sections"] = [s for s in answer["sections"] if s["items"]
                                  and (not args.section or args.section.lower() in s["title"].lower())]
        elif args.cmd in ("done", "undo"):
            answer = set_done(args.vault, args.id, args.cmd == "done")
        elif args.cmd == "add":
            answer = add(args.vault, args.section, args.text)
        else:
            answer = status(args.logs)
    except (Refused, OSError) as e:
        print(json.dumps({"ok": False, "error": str(e)}))
        sys.exit(1)
    print(json.dumps(answer, default=str))


if __name__ == "__main__":
    main()

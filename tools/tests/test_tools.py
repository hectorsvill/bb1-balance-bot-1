"""test_tools.py - checks for our Python tools, run by CI (.github/workflows/compile.yml) and by hand.

Usage (from the repo folder):
    tools/venv/bin/python -m unittest discover -s tools/tests -v
    tools/venv/bin/python -m unittest tools/tests/test_tools.py        # the same

What it checks, without the robot and without our private logs:
  1. every tools/*.py compiles
  2. runs_db.py builds a catalog from two small made-up logs (make_fixtures.py
     writes them to a temp folder: real logs never go in this public repo)
     and fills every table
  3. logs.py answers schema / runs / show / ticks / download / sql in JSON from
     that catalog, and refuses anything that writes
  4. bb1.py against tools/mock_bb1.py (started here on a free port): state,
     api, arm, drive, stop, disarm, and the safety refusals and caps
  5. bb1_mcp.py (only if the mcp package is installed: Python 3.10+): the same
     through MCP tools, in memory and over stdio like a real agent client

It never talks to the real robot: every bb1.py call goes to localhost.
Only the standard library is used here (unittest, subprocess, sqlite3).
"""

import asyncio
import json
import os
import py_compile
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.parse
import urllib.request
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
REPO = TOOLS.parent
FIXTURES = Path(tempfile.mkdtemp(prefix="bb1_test_fixtures_"))   # made-up logs, written below
PY = sys.executable                      # the same Python (venv) that runs the tests
sys.path.insert(0, str(TOOLS))           # so bb1, logs and bb1_mcp import like the tools import each other

try:
    import mcp                           # noqa: F401  (Python 3.10+ only)
    HAVE_MCP = True
except ImportError:
    HAVE_MCP = False

from make_fixtures import TICKS as FIXTURE_TICKS, FALL_NOTE, write_fixtures   # noqa: E402
FIXTURE_LOGS = sorted(write_fixtures(FIXTURES))


def tool(script, *args, timeout=60):
    """Run tools/<script> with args; returns (exit code, stdout, stderr)."""
    res = subprocess.run([PY, str(TOOLS / script), *args], capture_output=True, text=True,
                         timeout=timeout, cwd=REPO)
    return res.returncode, res.stdout, res.stderr


def json_tool(script, *args, **kw):
    """Run a JSON tool: (exit code, the parsed answer). Fails the test if stdout isn't one JSON document."""
    code, out, err = tool(script, *args, **kw)
    try:
        return code, json.loads(out)
    except ValueError:
        raise AssertionError(f"{script} {' '.join(args)}: not JSON (exit {code}):\n{out}\n{err}")


# --- the catalog: built once from the fixtures, in a temp folder -------------------------------

catalog_dir = None


def build_catalog():
    """Copy the fixtures to a temp folder and build runs.sqlite there (never in the repo)."""
    global catalog_dir
    if catalog_dir is None:
        catalog_dir = Path(tempfile.mkdtemp(prefix="bb1_test_logs_"))
        for f in FIXTURES.iterdir():
            shutil.copy(f, catalog_dir / f.name)
        code, out, err = tool("runs_db.py", "build", "--logs", str(catalog_dir))
        assert code == 0, f"runs_db.py build failed:\n{out}\n{err}"
    return catalog_dir / "runs.sqlite"


# --- the mock robot: started once on a free port ------------------------------------------------

mock = None
mock_host = None


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_mock():
    global mock, mock_host
    if mock is None:
        port = free_port()
        mock = subprocess.Popen([PY, str(TOOLS / "mock_bb1.py"), "--port", str(port)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        mock_host = f"localhost:{port}"
        end = time.time() + 10
        while True:                                        # wait until it answers
            try:
                urllib.request.urlopen(f"http://{mock_host}/state", timeout=1).read()
                break
            except OSError:
                if time.time() > end or mock.poll() is not None:
                    raise RuntimeError("tools/mock_bb1.py didn't start")
                time.sleep(0.1)
    return mock_host


def mock_post(path, **fields):
    data = urllib.parse.urlencode(fields).encode()
    return urllib.request.urlopen(f"http://{mock_host}{path}", data=data, timeout=3).read().decode()


def arm_mock(arm):
    """The mock robot rocks ±25 deg and refuses to arm unless within 10 deg of trim, like
    the real one. Move trim to where it is now ("here"), then arm; try a few times."""
    for _ in range(5):
        mock_post("/gains", trim="here")
        code, answer = arm()
        if answer.get("armed"):
            return answer
    raise AssertionError(f"the mock never armed: {answer}")


def tearDownModule():
    if mock is not None:
        mock.terminate()
        mock.wait(timeout=5)
    if catalog_dir is not None:
        shutil.rmtree(catalog_dir, ignore_errors=True)
    shutil.rmtree(FIXTURES, ignore_errors=True)


# --- 1. every tool compiles ----------------------------------------------------------------------

class CompileTest(unittest.TestCase):
    def test_every_tool_compiles(self):
        scripts = sorted(TOOLS.glob("*.py")) + sorted((TOOLS / "tests").glob("*.py"))
        self.assertGreater(len(scripts), 10)
        for path in scripts:
            with self.subTest(path.name):
                # cfile in a temp folder: don't leave __pycache__ in the repo
                with tempfile.TemporaryDirectory() as tmp:
                    py_compile.compile(str(path), cfile=str(Path(tmp) / "x.pyc"), doraise=True)


# --- 2. runs_db.py: the fixture catalog -----------------------------------------------------------

class NoLogsInRepoTest(unittest.TestCase):
    def test_no_logs_are_committed(self):
        # This repo is public: recorded runs live in the private repo bb1-private.
        # .gitignore keeps *.csv out; this catches one added with git add -f.
        try:
            tracked = subprocess.run(["git", "ls-files", "*.csv", "*.sqlite", "logs"], cwd=REPO,
                                     capture_output=True, text=True, timeout=10).stdout.split()
        except (OSError, subprocess.TimeoutExpired):
            self.skipTest("git isn't available")
        self.assertEqual(tracked, [], "log files are tracked in the public repo")


class RunsDbTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = sqlite3.connect(build_catalog())

    @classmethod
    def tearDownClass(cls):
        cls.db.close()

    def count(self, sql, *params):
        return self.db.execute(sql, params).fetchone()[0]

    def test_every_table_is_there(self):
        tables = {r[0] for r in self.db.execute("SELECT name FROM sqlite_master WHERE type = 'table'")}
        self.assertEqual(tables, {"runs", "ticks", "disarms", "steps", "gains", "meta", "flashes", "commands"})

    def test_runs(self):
        files = [r[0] for r in self.db.execute("SELECT file FROM runs ORDER BY file")]
        self.assertEqual(files, FIXTURE_LOGS)
        carpet = self.db.execute("SELECT surface, log_format, kp, ki, sdi, th, rows FROM runs "
                                 "WHERE file LIKE '%carpet%'").fetchone()
        self.assertEqual(carpet, ("carpet", 11, 18.0, 100.0, 4.0, 60.0, FIXTURE_TICKS))

    def test_ticks(self):
        self.assertEqual(self.count("SELECT count(*) FROM ticks"), 2 * FIXTURE_TICKS)
        # t_s counts from 0 at 200 Hz: 60 ticks = 0 .. 0.295 s
        self.assertAlmostEqual(self.count("SELECT max(t_s) FROM ticks"), (FIXTURE_TICKS - 1) / 200)
        # format 9 has no speed_pwm column: NULL there, a number in format 11
        self.assertEqual(self.count("SELECT count(speed_pwm) FROM ticks"), FIXTURE_TICKS)

    def test_the_fall_is_a_disarm(self):
        row = self.db.execute("SELECT d.reason, d.reason_text, d.tick, r.outcome FROM disarms d "
                              "JOIN runs r USING (run_id) WHERE r.file LIKE '%fall%'").fetchone()
        self.assertEqual(row, (2, "fell", 47070, "fell"))

    def test_steps_gains_meta(self):
        self.assertEqual([r[0] for r in self.db.execute("SELECT name FROM steps ORDER BY n")], ["pause", "fwd 0.3"])
        self.assertEqual(self.count("SELECT count(*) FROM gains"), 2)          # one version per log
        self.assertGreater(self.count("SELECT count(*) FROM meta WHERE name = 'firmware'"), 1)

    def test_side_files(self):
        self.assertEqual(self.count("SELECT count(*) FROM flashes"), 2)
        self.assertEqual(self.db.execute("SELECT cmd, ok FROM commands").fetchall(), [("state", 1)])

    def test_mock_logs_are_skipped(self):
        # A log that says "firmware,bb1 MOCK" never goes in (mock data never mixes with real data)
        with tempfile.TemporaryDirectory() as tmp:
            src = (FIXTURES / FIXTURE_LOGS[0]).read_text()
            Path(tmp, "bb1_2026-01-01_00-00-00_mock.csv").write_text(
                src.replace("# firmware,bb1 milestone 2", "# firmware,bb1 MOCK (tools/mock_bb1.py)"))
            code, out, err = tool("runs_db.py", "build", "--logs", tmp)
            self.assertEqual(code, 0, err)
            self.assertIn("1 mock skipped", out)


# --- 3. logs.py: JSON answers from the fixture catalog --------------------------------------------

class LogsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = str(build_catalog())

    def logs(self, *args):
        return json_tool("logs.py", "--db", self.db, *args)

    def test_schema(self):
        code, a = self.logs("schema")
        self.assertEqual(code, 0)
        self.assertTrue(a["ok"])
        self.assertEqual(a["tables"]["ticks"]["rows"], 2 * FIXTURE_TICKS)
        self.assertIn("pitch", a["tables"]["ticks"]["columns"])

    def test_runs(self):
        code, a = self.logs("runs")
        self.assertEqual(code, 0)
        self.assertEqual(len(a["runs"]), 2)
        self.assertEqual(a["runs"][0]["surface"], "carpet")               # newest first
        code, a = self.logs("runs", "--where", "outcome = 'fell'")
        self.assertEqual([r["surface"] for r in a["runs"]], ["wood"])

    def test_show(self):
        code, a = self.logs("show", "ci-wood-fall")                        # a part of the file name
        self.assertEqual(code, 0)
        self.assertEqual(a["disarms"][0]["reason_text"], "fell")
        self.assertIn("speed_tps", a["tick_columns"])
        self.assertIn(f"note,{FALL_NOTE}", a["header"])
        code, a = self.logs("show", "ci-")                                  # matches both: refused
        self.assertEqual(code, 1)
        self.assertFalse(a["ok"])

    def test_ticks(self):
        code, a = self.logs("ticks", "ci-carpet", "--cols", "pitch,speed_tps", "--limit", "10")
        self.assertEqual(code, 0)
        self.assertEqual(a["columns"], ["t_s", "pitch", "speed_tps"])
        self.assertEqual((a["rows_total"], a["every"], a["returned"]), (FIXTURE_TICKS, 6, 10))
        code, a = self.logs("ticks", "ci-carpet", "--from", "0.1", "--to", "0.2")
        self.assertEqual(a["rows_total"], 21)                               # 0.100 .. 0.200 s at 5 ms
        code, a = self.logs("ticks", "ci-carpet", "--cols", "no_such_column")
        self.assertEqual(code, 1)

    def test_download(self):
        with tempfile.TemporaryDirectory() as tmp:
            whole, part = Path(tmp, "whole.csv"), Path(tmp, "slice.csv")
            code, a = self.logs("download", "ci-carpet", "--out", str(whole))
            self.assertEqual(code, 0)
            self.assertEqual(whole.read_text(), (FIXTURES / FIXTURE_LOGS[1]).read_text())   # word for word
            code, a = self.logs("download", "ci-carpet", "--cols", "pitch", "--out", str(part))
            self.assertEqual((code, a["rows"]), (0, FIXTURE_TICKS))
            self.assertEqual(part.read_text().splitlines()[0], "t_s,pitch")

    def test_sql(self):
        code, a = self.logs("sql", "SELECT surface, count(*) AS n FROM runs GROUP BY surface ORDER BY surface")
        self.assertEqual(code, 0)
        self.assertEqual(a["rows"], [["carpet", 1], ["wood", 1]])

    def test_sql_refuses_writes(self):
        for query in ("DELETE FROM runs", "DROP TABLE ticks", "SELECT 1; DELETE FROM runs",
                      "WITH x AS (SELECT 1) DELETE FROM runs", "UPDATE runs SET kp = 0"):
            with self.subTest(query):
                code, a = self.logs("sql", query)
                self.assertEqual(code, 1)
                self.assertFalse(a["ok"])
        code, a = self.logs("sql", "SELECT count(*) FROM runs")
        self.assertEqual(a["rows"], [[2]])                                  # nothing was deleted


# --- 4. bb1.py against the mock robot --------------------------------------------------------------

class Bb1MockTest(unittest.TestCase):
    """In order (test_1 ... test_6): the mock keeps its state between them."""

    @classmethod
    def setUpClass(cls):
        cls.host = start_mock()
        log = REPO / "logs" / "agent_commands.csv"
        cls.command_log = log
        cls.command_log_before = log.stat().st_size if log.exists() else None

    def bb1(self, *args, timeout=30):
        return json_tool("bb1.py", "--host", self.host, *args, timeout=timeout)

    def test_1_state_and_api(self):
        code, a = self.bb1("state")
        self.assertEqual(code, 0)
        for field in ("armed", "reason", "auto_state", "pitch_deg", "speed", "position_turns",
                      "drive", "enc_fault", "batt_v_per_cell"):
            self.assertIn(field, a["state"])
        code, a = self.bb1("state", "--full")
        self.assertTrue(a["state"]["mock"])
        code, a = self.bb1("api")
        self.assertEqual(code, 0)
        self.assertEqual(a["firmware"], "bb1 MOCK")
        self.assertIn("drive_max_speed", a["limits"])

    def test_2_drive_refused_when_not_balancing(self):
        self.bb1("disarm")
        code, a = self.bb1("drive", "--speed", "0.2", "--for", "0.3")
        self.assertEqual(code, 1)
        self.assertIn("not balancing", a["error"])

    def test_3_nothing_to_drive(self):
        code, a = self.bb1("drive", "--for", "0.3")
        self.assertEqual(code, 1)
        self.assertIn("nothing to drive", a["error"])

    def test_4_arm_and_drive(self):
        arm_mock(lambda: self.bb1("arm"))
        code, a = self.bb1("drive", "--speed", "0.2", "--turn", "-0.1", "--for", "0.4")
        self.assertEqual(code, 0, a)
        self.assertTrue(a["ok"])
        self.assertEqual(a["asked"], {"speed": 0.2, "turn": -0.1, "secs": 0.4})
        self.assertEqual(a["cut"], [])
        self.assertTrue(a["state"]["armed"])

    def test_5_caps_cut_and_say_so(self):
        code, a = self.bb1("drive", "--speed", "0.9", "--turn", "-0.9", "--for", "2",
                           "--max", "0.3", "--max-turn", "0.4", "--max-for", "0.3")
        self.assertEqual(code, 0, a)
        self.assertEqual(a["asked"], {"speed": 0.3, "turn": -0.4, "secs": 0.3})
        self.assertEqual(len(a["cut"]), 3, a["cut"])
        # --max-for can only lower the tool's own 10 s
        code, a = self.bb1("drive", "--speed", "0.1", "--for", "0.2", "--max-for", "60")
        self.assertEqual(a["asked"]["secs"], 0.2)
        # --max alone caps turn too (as before --max-turn existed)
        code, a = self.bb1("drive", "--turn", "0.9", "--for", "0.2", "--max", "0.25")
        self.assertEqual(a["asked"]["turn"], 0.25)

    def test_6_stop_and_disarm(self):
        code, a = self.bb1("stop")
        self.assertEqual((code, a), (0, {"ok": True, "stopped": True}))
        code, a = self.bb1("disarm")
        self.assertEqual(code, 0)
        code, a = self.bb1("state")
        self.assertFalse(a["state"]["armed"])
        self.assertEqual(a["state"]["reason"], "disarmed by you")

    def test_7_mock_calls_are_not_recorded(self):
        log = self.command_log
        self.assertEqual(log.stat().st_size if log.exists() else None, self.command_log_before)

    def test_8_no_answer_is_exit_2(self):
        code, a = json_tool("bb1.py", "--host", f"localhost:{free_port()}", "state")
        self.assertEqual(code, 2)
        self.assertIn("didn't answer", a["error"])

    def test_9_the_mock_is_never_recorded(self):
        for action in ("status", "start", "stop"):
            code, a = self.bb1("log", action)
            self.assertEqual(code, 1)
            self.assertIn("mock is never recorded", a["error"])


# --- 4b. session_log.py: the recorder bb1.py starts ------------------------------------------------

class SessionLogTest(unittest.TestCase):
    """The recorder loop against the mock, with every file it writes moved to a temp
    folder (the real tool refuses the mock, so nothing here can reach logs/)."""

    def test_records_and_saves(self):
        import signal
        import threading
        import session_log
        start_mock()
        tmp = Path(tempfile.mkdtemp())
        saved_files = {}
        old = {k: getattr(session_log, k) for k in ("DIR", "STATUS", "LABEL", "LAST", "LAST_COMMAND", "log_path", "save_log")}
        handlers = signal.getsignal(signal.SIGTERM), signal.getsignal(signal.SIGINT)
        try:
            session_log.DIR = tmp
            session_log.STATUS, session_log.LABEL, session_log.LAST = tmp / "status.json", tmp / "label.json", tmp / "last.json"
            session_log.LAST_COMMAND = tmp / "last_command"
            session_log.log_path = lambda note, started: tmp / "bb1_test.csv"
            session_log.save_log = lambda out, meta, note, started, header, rows, lost, versions, surface: \
                saved_files.update(out=out, header=header, rows=rows, note=note, surface=surface, versions=versions)
            session_log.set_label(note="ci test", surface="wood")
            session_log.touch_command()
            session_log.stop_asked = False
            threading.Timer(1.5, session_log.ask_stop, (None, None)).start()   # like 'bb1.py log stop'
            session_log.record(f"http://{mock_host}", mock_host)
        finally:
            for k, v in old.items():
                setattr(session_log, k, v)
            signal.signal(signal.SIGTERM, handlers[0])
            signal.signal(signal.SIGINT, handlers[1])
        last = json.loads((tmp / "last.json").read_text())
        shutil.rmtree(tmp, ignore_errors=True)
        self.assertEqual(last["stopped"], "asked to stop")
        self.assertGreater(len(saved_files["rows"]), 200)                  # 2 s of look-back plus 1.5 s
        self.assertTrue(saved_files["header"].startswith("tick,"))
        ticks = [int(r.split(",")[0]) for r in saved_files["rows"]]
        self.assertEqual(ticks, list(range(ticks[0], ticks[0] + len(ticks))))   # no gaps
        self.assertEqual((saved_files["note"], saved_files["surface"]), ("ci test", "wood"))
        self.assertTrue(saved_files["versions"])                           # the gains went in


# --- 5. bb1_mcp.py: the same through MCP -----------------------------------------------------------

@unittest.skipUnless(HAVE_MCP, "the mcp package isn't installed (needs Python 3.10+)")
class McpTest(unittest.TestCase):
    TOOLS = {"bb1_api", "bb1_state", "bb1_arm", "bb1_drive", "bb1_stop", "bb1_disarm", "bb1_log",
             "logs_schema", "logs_runs", "logs_show", "logs_ticks", "logs_sql"}

    @classmethod
    def setUpClass(cls):
        import bb1_mcp
        cls.host = start_mock()
        cls.server = bb1_mcp.make_server(cls.host, max_speed=0.3, max_turn=0.4, max_for=0.5, db=build_catalog())

    def calls(self, steps):
        """Run (tool, arguments) steps on one in-memory client session; returns each answer dict."""
        from mcp.client import Client

        async def go():
            answers = []
            async with Client(self.server) as client:
                for name, arguments in steps:
                    res = await client.call_tool(name, arguments)
                    self.assertFalse(res.is_error, f"{name}: {res.content}")
                    answers.append(res.structured_content)
            return answers
        return asyncio.run(go())

    def test_tools_listed(self):
        from mcp.client import Client

        async def go():
            async with Client(self.server) as client:
                return await client.list_tools()
        listed = asyncio.run(go()).tools
        self.assertEqual({t.name for t in listed}, self.TOOLS)
        drive = next(t for t in listed if t.name == "bb1_drive")
        self.assertIn("capped at 0.3", drive.description)                 # the server's caps, in the description

    def test_robot_tools(self):
        state, api = self.calls([("bb1_state", {}), ("bb1_api", {})])
        self.assertEqual(state["exit_code"], 0)
        self.assertIn("armed", state["state"])
        self.assertEqual(api["firmware"], "bb1 MOCK")
        self.calls([("bb1_disarm", {})])
        refused, = self.calls([("bb1_drive", {"speed": 0.2, "secs": 0.2})])
        self.assertEqual(refused["exit_code"], 1)
        self.assertIn("not balancing", refused["error"])
        arm_mock(lambda: (0, self.calls([("bb1_arm", {})])[0]))
        drove, stop, off = self.calls([("bb1_drive", {"speed": 5, "turn": 5, "secs": 9}),
                                       ("bb1_stop", {}), ("bb1_disarm", {})])
        self.assertTrue(drove["ok"], drove)
        self.assertEqual(drove["asked"], {"speed": 0.3, "turn": 0.4, "secs": 0.5})   # an agent can't pass the caps
        self.assertEqual(len(drove["cut"]), 3)
        self.assertTrue(stop["stopped"])
        self.assertFalse(off["armed"])

    def test_log_tools(self):
        schema, runs, show, ticks, sql, write = self.calls([
            ("logs_schema", {}), ("logs_runs", {"where": "surface = 'wood'"}), ("logs_show", {"run": "ci-carpet"}),
            ("logs_ticks", {"run": "ci-carpet", "cols": ["pitch"], "limit": 5}),
            ("logs_sql", {"query": "SELECT count(*) AS n FROM ticks"}), ("logs_sql", {"query": "DELETE FROM runs"})])
        self.assertIn("commands", schema["tables"])
        self.assertEqual(len(runs["runs"]), 1)
        self.assertEqual([s["name"] for s in show["steps"]], ["pause", "fwd 0.3"])
        self.assertEqual(ticks["returned"], 5)
        self.assertEqual(sql["rows"], [[2 * FIXTURE_TICKS]])
        self.assertFalse(write["ok"])                                       # read only

    def test_over_stdio(self):
        # Like an agent client: start bb1_mcp.py as a subprocess and talk MCP over its stdin/stdout
        from mcp.client import Client
        from mcp.client.stdio import StdioServerParameters
        params = StdioServerParameters(command=PY, args=[str(TOOLS / "bb1_mcp.py"), "--host", self.host,
                                                         "--db", str(build_catalog())], cwd=str(REPO))

        async def go():
            async with Client(params) as client:
                listed = await client.list_tools()
                res = await client.call_tool("bb1_state", {})
                return {t.name for t in listed.tools}, res
        names, res = asyncio.run(go())
        self.assertEqual(names, self.TOOLS)
        self.assertFalse(res.is_error, res.content)


if __name__ == "__main__":
    unittest.main()

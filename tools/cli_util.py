"""cli_util.py - the same --json flag and exit codes for every script in tools/.

Exit codes
  0    ok
  1    a check or a test failed (the robot refused, a log is incomplete,
       the compile or the upload failed, a safety check said no)
  2    usage or configuration (bad arguments, a file you still have to
       create, a question when stdin is not a terminal)
  130  stopped by Ctrl-C (128 + SIGINT). deadband_test.py uses this after it
       stops the motors. bb1.py keeps its own contract of only 0, 1 and 2
       (Ctrl-C there is 1): see the top of bb1.py.

--json
  One JSON document on stdout. Human text otherwise.
  bb1.py, logs.py and todo.py always print JSON, because that is the
  interface agents call. They accept --json so every tool takes the same
  flag; passing it does not change what they print.
  bb1_mcp.py speaks MCP on stdout. --json is accepted and ignored.
  handheld.py is a live keyboard UI: --json only changes its error
  messages. The status line stays text.
  session_log.py is the recorder bb1.py starts. --json is accepted and
  ignored; bb1.py owns the JSON.

Put --json before the subcommand on tools that have one (bb1.py, logs.py,
todo.py, runs_db.py), the same place as --host and --db. It is also
accepted after the subcommand.
"""

import argparse
import json
import sys

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_USAGE = 2
EXIT_INTERRUPT = 130


def add_json(ap, always=False):
    """Add --json unless this parser already has it (main parser and its subcommands)."""
    if any(getattr(a, "dest", None) == "json" for a in ap._actions):
        return ap
    if always:
        help_text = "accepted; this tool always prints one JSON document"
    else:
        help_text = "print one JSON document on stdout (human text otherwise)"
    ap.add_argument("--json", action="store_true", help=help_text)
    return ap


def add_json_everywhere(ap, always=False):
    """--json on this parser and on every subcommand, so it can sit on either side."""
    add_json(ap, always=always)
    for action in ap._actions:
        if isinstance(action, argparse._SubParsersAction):
            for sub in action.choices.values():
                add_json(sub, always=always)
    return ap


def install_json_errors(ap):
    """Bad arguments exit 2. With --json on the command line, that error is JSON on stdout.

    In-process callers (the MCP server) do not put --json in sys.argv, so they
    still get the usual text on stderr and do not write onto the MCP stream.
    """
    if getattr(ap, "_json_errors", False):
        return ap
    ap._json_errors = True

    def error(message, parser=ap):
        as_json = "--json" in sys.argv[1:]
        if as_json:
            sys.stdout.write(json.dumps({"ok": False, "error": message, "exit_code": EXIT_USAGE}) + "\n")
        else:
            parser.print_usage(sys.stderr)
            sys.stderr.write(f"{parser.prog}: error: {message}\n")
        raise SystemExit(EXIT_USAGE)

    ap.error = error
    for action in ap._actions:
        if isinstance(action, argparse._SubParsersAction):
            for sub in action.choices.values():
                install_json_errors(sub)
    return ap


def standard_cli(ap, always_json=False):
    """Call once, after the subcommands exist, just before parse_args."""
    add_json_everywhere(ap, always=always_json)
    install_json_errors(ap)
    return ap


def fail(message, code=EXIT_FAIL, as_json=False):
    """Print the error and stop. JSON on stdout when as_json, otherwise text on stderr."""
    if as_json:
        print(json.dumps({"ok": False, "error": message, "exit_code": code}))
    else:
        print(message, file=sys.stderr)
    raise SystemExit(code)


def emit(payload, as_json, human=None):
    """One JSON document, or the human text. human may be several lines."""
    if as_json:
        print(json.dumps(payload, default=str))
    elif human:
        print(human)

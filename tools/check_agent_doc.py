"""check_agent_doc.py - is docs/AGENT.md written for the API the firmware has now?

    python3 tools/check_agent_doc.py

docs/AGENT.md is the manual an AI agent gets to drive BB1. Its line
"**Written for `/api` version: N**" must match API_VERSION in
firmware/bb1/api.h. API_VERSION goes up whenever an endpoint, argument or
limit changes, so a mismatch means the manual may tell the agent something
that's no longer true. CI runs this on every pull request: update AGENT.md,
then set its version line, and the check passes again.

Only the Python standard library is used (CI runs it without the venv), plus
tools/cli_util.py which is also standard library only.
Exit code 0 = they match, 1 = they don't (or a line is missing), 2 = bad arguments.
--json prints {"ok": true, "api_version": N} instead of the sentence.
"""

import argparse
import json
import re
from pathlib import Path

import cli_util

REPO = Path(__file__).resolve().parent.parent
AGENT_MD = REPO / "docs" / "AGENT.md"
API_H = REPO / "firmware" / "bb1" / "api.h"


def find(path, pattern, what):
    m = re.search(pattern, path.read_text())
    if not m:
        raise LookupError(f"{path.relative_to(REPO)}: can't find {what}")
    return int(m.group(1))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli_util.standard_cli(ap)
    args = ap.parse_args(argv)
    try:
        firmware = find(API_H, r"const int API_VERSION = (\d+);", "the API_VERSION line")
        manual = find(AGENT_MD, r"Written for `/api` version: (\d+)", "the 'Written for `/api` version: N' line")
    except LookupError as e:
        cli_util.fail(str(e), cli_util.EXIT_FAIL, args.json)
    if firmware != manual:
        cli_util.fail(
            f"docs/AGENT.md is written for /api version {manual}, but api.h says API_VERSION {firmware}.\n"
            "Update AGENT.md for what changed (its last section lists what to check), then set its version line.",
            cli_util.EXIT_FAIL, args.json)
    if args.json:
        print(json.dumps({"ok": True, "api_version": firmware}))
    else:
        print(f"docs/AGENT.md matches /api version {firmware}")


if __name__ == "__main__":
    main()

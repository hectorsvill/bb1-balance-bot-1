"""check_agent_doc.py - is docs/AGENT.md written for the API the firmware has now?

    python3 tools/check_agent_doc.py

docs/AGENT.md is the manual an AI agent gets to drive BB1. Its line
"**Written for `/api` version: N**" must match API_VERSION in
firmware/bb1/api.h. API_VERSION goes up whenever an endpoint, argument or
limit changes, so a mismatch means the manual may tell the agent something
that's no longer true. CI runs this on every pull request: update AGENT.md,
then set its version line, and the check passes again.

Only the Python standard library is used (CI runs it without the venv).
Exit code 0 = they match, 1 = they don't (or a line is missing).
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
AGENT_MD = REPO / "docs" / "AGENT.md"
API_H = REPO / "firmware" / "bb1" / "api.h"


def find(path, pattern, what):
    m = re.search(pattern, path.read_text())
    if not m:
        sys.exit(f"{path.relative_to(REPO)}: can't find {what}")
    return int(m.group(1))


def main():
    firmware = find(API_H, r"const int API_VERSION = (\d+);", "the API_VERSION line")
    manual = find(AGENT_MD, r"Written for `/api` version: (\d+)", "the 'Written for `/api` version: N' line")
    if firmware != manual:
        sys.exit(f"docs/AGENT.md is written for /api version {manual}, but api.h says API_VERSION {firmware}.\n"
                 "Update AGENT.md for what changed (its last section lists what to check), then set its version line.")
    print(f"docs/AGENT.md matches /api version {firmware}")


if __name__ == "__main__":
    main()

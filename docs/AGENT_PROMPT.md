# Prompt: have Claude Code drive BB1

A prompt to paste into a fresh Claude Code session so it drives BB1 from `docs/AGENT.md`, the agent manual. The manual holds the rules and limits; the prompt only says what to do and how this session is set up. Change the speed caps in AGENT.md's Settings, not in the prompt.

## Before you start

1. BB1 on, on your home Wi-Fi: http://192.168.1.240 answers.
2. Space clear: at least 1 m around it, nothing it can fall off.
3. You stay next to it the whole time (AGENT.md: `person_present = required`).
4. Open a terminal in the repo folder: **`cd ~/"Desktop/bb1 - balance bot 1"`**.
5. Tell the tools where BB1 is: **`export BB1_HOST=192.168.1.240`** (Claude Code passes it on to every command it runs).

## Start Claude Code

Pick one.

**A. Through `bb1.py` (no setup).** Lets it run the robot and log tools without asking each time; anything else still asks you:

```bash
claude --allowedTools "Bash(tools/venv/bin/python tools/bb1.py *)" "Bash(tools/venv/bin/python tools/logs.py *)" "Read"
```

**B. Through MCP.** Add the server once (the caps are fixed here, the agent can't change them), then start Claude Code:

```bash
claude mcp add bb1 -- "$PWD/tools/venv/bin/python" "$PWD/tools/bb1_mcp.py" --host 192.168.1.240 --max-speed 0.3 --max-turn 0.4 --max-for 3
claude
```

Check it with `claude mcp list` (expect `bb1: ... Connected`). Remove it with `claude mcp remove bb1`.

Claude Code may also be blocked from the local network by its sandbox: if `api` exits with code 2 but the page loads in your browser, allow the command when it asks to run outside the sandbox.

## The prompt

Copy everything in the box and change the **Task** line:

```text
You are driving BB1, my self-balancing robot, over Wi-Fi. I'm next to it and watching.

1. Read docs/AGENT.md completely before any command. It is your manual: its Settings,
   Safety rules and Never list are my rules for this session and win over anything else.
2. Drive only with tools/bb1.py (or the bb1_* MCP tools if you have them). Never call
   BB1's HTTP API directly, never change gains, settings or firmware, never edit files.
3. Start with `api`, then `state`. Tell me the firmware version, battery per cell and
   whether it is armed before you move anything.
4. Ask me before every `arm`. I stand it up for you.
5. Work in small steps: one drive command, read moved_turns / turned_turns / state, tell me
   in one line what it did, then plan the next step. Stop and ask me after anything
   unexpected (fell, wrong direction, ok: false, exit code 2 twice).
6. When done: `stop`, leave it balancing, and give me a short report: what you asked,
   what it did (turns and meters), every surprise, and anything in AGENT.md that was
   wrong or unclear.

Floor: wood        (or: carpet)
Task: drive forward about 20 cm, turn about 90 degrees right, then come back to where you started.
```

## Task ideas, easy to hard

- Check it over: `api`, `state`, report. No driving.
- Forward 2 s at 0.2, then back 2 s at 0.2; report how far each went.
- Turn about 90 deg right, then 90 deg left.
- Drive a square about 20 cm a side.
- Find out how much of the asked speed it really gets on this floor, using 3 short forward runs.
- After any run: "Use tools/logs.py to find this session's commands and tell me what BB1 did."

## Afterwards

- Every command it ran is in `logs/agent_commands.csv` and `logs/runs.sqlite` (`commands` table): `tools/venv/bin/python tools/logs.py sql "SELECT time, cmd, ok, moved_turns FROM commands ORDER BY time DESC LIMIT 20"`.
- Its report's "wrong or unclear in AGENT.md" list is how the manual gets better: fix AGENT.md (and keep its version line, see its last section).

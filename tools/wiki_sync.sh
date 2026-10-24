#!/bin/bash
# wiki_sync.sh - copy docs/ to the GitHub wiki (github.com/hectorsvill/bb1-balance-bot-1/wiki).
#
# The repo's docs/ stay the source of truth: edit them there, then run this to
# refresh the wiki. Never edit the wiki pages by hand: the next sync overwrites them.
#
# What it does:
#   1. clones the wiki (it's its own git repo: <repo>.wiki.git) into a temp folder
#   2. copies every docs/*.md as a page (PID_LESSON.md -> page "PID_LESSON"), plus images
#   3. fixes links: "FOO.md" -> "FOO" (wiki page), "../firmware/x" -> the file on GitHub
#   4. writes Home (the index) and _Sidebar, commits and pushes
#
# Run from the repo root:  tools/wiki_sync.sh
# The wiki must exist first: on GitHub, Wiki tab -> "Create the first page" -> Save (once).

set -euo pipefail
REPO_URL="https://github.com/hectorsvill/bb1-balance-bot-1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone -q "$REPO_URL.wiki.git" "$WORK/wiki"
cd "$WORK/wiki"
git rm -q -r --ignore-unmatch . >/dev/null       # start clean: pages removed from docs/ go away too

cp "$ROOT"/docs/*.png "$ROOT"/docs/*.svg . 2>/dev/null || true
for f in "$ROOT"/docs/*.md; do
  # Link fixes (sed -E, macOS and Linux):
  #   ](FOO.md) / ](FOO.md#part)    -> ](FOO) / ](FOO#part)        another wiki page
  #   ](../path) / ](../path#part)  -> ](REPO_URL/blob/main/path)  a file in the repo
  sed -E \
    -e 's#\]\(([A-Za-z0-9_-]+)\.md(\#[^)]*)?\)#](\1\2)#g' \
    -e "s#\]\(\.\./([^)]*)\)#](${REPO_URL}/blob/main/\1)#g" \
    "$f" > "$(basename "$f")"
done

# Home: the README's intro is the landing page, then the list of pages.
{
  echo "# BB1 - balance bot 1"
  echo
  echo "Two-wheel self-balancing robot (Pico W, D153C, MPU-6050). These pages are copied"
  echo "from the repo's [docs/](${REPO_URL}/tree/main/docs) by \`tools/wiki_sync.sh\`;"
  echo "the repo is the source of truth."
  echo
  echo "## Pages"
  echo
  for f in "$ROOT"/docs/*.md; do
    name="$(basename "$f" .md)"
    title="$(grep -m1 '^# ' "$f" | sed 's/^# //')"
    echo "- [${title:-$name}]($name)"
  done
} > Home.md
{ echo "**[Home](Home)**"; echo; for f in "$ROOT"/docs/*.md; do n="$(basename "$f" .md)"; echo "- [$n]($n)"; done; } > _Sidebar.md

git add -A
if git diff --cached --quiet; then echo "wiki already up to date"; exit 0; fi
git commit -q -m "Sync from docs/ ($(cd "$ROOT" && git rev-parse --short HEAD))"
git push -q origin HEAD
echo "wiki updated: $REPO_URL/wiki"

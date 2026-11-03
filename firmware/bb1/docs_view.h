// docs_view.h - shows a Markdown doc (docs_pages.h) as a web page in a browser.
//
// A browser asking for /agent, /api or /api.md gets:
//   DOCS_HEAD  +  the doc's Markdown, untouched  +  DOCS_TAIL
// The Markdown sits inside <script type="text/plain">, so the page carries the
// doc only once (no HTML copy in flash) and a program that reads the HTML
// still sees the full text. The small renderer in DOCS_TAIL turns it into
// headings, tables, lists and code when the page loads. Nothing is loaded from
// the internet, so it works on BB1's own network too.
//
// The renderer knows only what our docs use: # headings, ``` code blocks,
// | tables |, - and 1. lists, paragraphs, `code`, **bold** and [links](url).
// tools/gen_docs.py refuses a doc containing "</script", which would end the
// text early.

#pragma once

const char DOCS_HEAD[] = R"DOCS(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BB1 docs</title>
<style>
  :root { --bg:#0e1116; --panel:#161b22; --panel2:#1c222b; --line:#262d37; --text:#e6edf3; --muted:#8b96a5; --blue:#4ea1ff; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:15px/1.55 -apple-system, system-ui, "Segoe UI", sans-serif; }
  nav { position:sticky; top:0; display:flex; flex-wrap:wrap; gap:6px 16px; padding:10px 16px; background:var(--panel); border-bottom:1px solid var(--line); }
  nav a { color:var(--blue); text-decoration:none; font-weight:600; }
  nav .raw { margin-left:auto; color:var(--muted); font-weight:400; }
  main { max-width:900px; margin:0 auto; padding:8px 16px 48px; }
  a { color:var(--blue); }
  h1, h2, h3, h4 { line-height:1.25; margin:1.4em 0 .5em; }
  h2 { padding-bottom:4px; border-bottom:1px solid var(--line); }
  code { background:var(--panel2); padding:1px 5px; border-radius:5px; font:13px ui-monospace, Menlo, monospace; }
  pre { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:12px; overflow-x:auto; }
  pre code { background:none; padding:0; }
  .table { overflow-x:auto; }               /* wide tables scroll inside, not the page */
  table { border-collapse:collapse; margin:8px 0; }
  th, td { border:1px solid var(--line); padding:6px 9px; text-align:left; vertical-align:top; }
  th { background:var(--panel2); }
  li { margin:3px 0; }
</style></head><body>
<nav><a href="/">BB1</a><a href="/agent">Agent manual</a><a href="/api">API</a><a id="raw" class="raw" href="?raw=1">raw</a></nav>
<main id="out"><noscript>JavaScript is off: the Markdown is below as plain text.</noscript></main>
<script type="text/plain" id="md">)DOCS";

const char DOCS_TAIL[] = R"DOCS(</script>
<script>
// Agents and scripts: add ?raw=1 (or curl without a browser's Accept header) for the plain Markdown.
const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

// One line of text: `code` first (nothing inside it is formatted), then **bold** and [links](url).
function inline(s) {
  return s.split('`').map((part, k) => k % 2 ? '<code>' + esc(part) + '</code>'
    : esc(part).replace(/\*\*(.+?)\*\*/g, '<b>$1</b>').replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, '<a href="$2">$1</a>')).join('');
}

const cells = row => row.trim().replace(/^\||\|$/g, '').split('|').map(c => c.trim());

function render(md) {
  const lines = md.split('\n'), out = [];
  let i = 0, para = [];
  const flush = () => { if (para.length) out.push('<p>' + inline(para.join(' ')) + '</p>'); para = []; };
  while (i < lines.length) {
    const line = lines[i];
    if (line.startsWith('```')) {                              // code block, as it is
      flush(); const code = [];
      for (i++; i < lines.length && !lines[i].startsWith('```'); i++) code.push(lines[i]);
      out.push('<pre><code>' + esc(code.join('\n')) + '</code></pre>'); i++; continue;
    }
    const h = line.match(/^(#{1,4})\s+(.*)/);
    if (h) {                                                  // heading, with an id to link to
      flush(); const id = h[2].toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
      out.push('<h' + h[1].length + ' id="' + id + '">' + inline(h[2]) + '</h' + h[1].length + '>'); i++; continue;
    }
    if (line.trim().startsWith('|')) {                        // table: header row, --- row, body rows
      flush(); const rows = [];
      for (; i < lines.length && lines[i].trim().startsWith('|'); i++) rows.push(cells(lines[i]));
      const body = rows.slice(2).map(r => '<tr>' + r.map(c => '<td>' + inline(c) + '</td>').join('') + '</tr>').join('');
      out.push('<div class="table"><table><tr>' + rows[0].map(c => '<th>' + inline(c) + '</th>').join('') + '</tr>' + body + '</table></div>');
      continue;
    }
    const li = line.match(/^(\s*)([-*]|\d+\.)\s+(.*)/);
    if (li) {                                                 // list; indented lines continue the item above
      flush(); const tag = /\d/.test(li[2]) ? 'ol' : 'ul', items = [];
      for (; i < lines.length; i++) {
        const m = lines[i].match(/^(\s*)([-*]|\d+\.)\s+(.*)/);
        if (m) items.push({ text: m[3], deep: m[1].length > 1 });
        else if (/^\s+\S/.test(lines[i]) && items.length) items[items.length - 1].text += ' ' + lines[i].trim();
        else break;
      }
      out.push('<' + tag + '>' + items.map(it => '<li' + (it.deep ? ' style="margin-left:20px"' : '') + '>' + inline(it.text) + '</li>').join('') + '</' + tag + '>');
      continue;
    }
    if (!line.trim()) flush(); else para.push(line.trim());   // blank line ends a paragraph
    i++;
  }
  flush();
  return out.join('\n');
}

const md = document.getElementById('md').textContent;
document.getElementById('out').innerHTML = render(md);
const title = md.match(/^#\s+(.*)/m);
if (title) document.title = title[1];
if (location.pathname === '/api') document.getElementById('raw').textContent = 'JSON';   // /api?raw=1 = the JSON list
if (location.hash) { const t = document.getElementById(location.hash.slice(1)); if (t) t.scrollIntoView(); }
</script></body></html>
)DOCS";

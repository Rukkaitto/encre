#!/usr/bin/env node
// Seed (or verify) design/ereader-v1-ui.html -- the published design canvas.
//
// THE CANVAS IS A GENERATED FILE. It is one HTML page holding two things: the
// Claude Design canvas editor, compiled (that is payload.template.html, ~2.4 MB,
// which this repo cannot rebuild and therefore keeps verbatim), and the design
// CONTENT -- every design/*.dc.html plus design/canvas.json -- as one JSON
// document in the `<script type="application/json" id="appifact-doc">` block.
// Seeding is: read the boards, serialise them into that block, write the page.
//
// WHY IT IS IN tools/ AND TRACKED. The original lived beside the design-change
// skill in .claude/skills/design-change/, untracked, and was LOST -- issue #60.
// Only SKILL.md was ever added to git, so the generator did not exist in any
// fresh clone or worktree (a worktree materialises tracked files only), and the
// documented reseed route could not be run at all. The canvas then went stale by
// hand-editing, which is the wrong operation on a generated file. tools/ is
// tracked and is where every other generator here lives, so the tool now
// survives the thing that killed it.
//
// EVERY CHECK BELOW IS A HARD ERROR, none is a warning, and the reason is the
// defect this replaced: a canvas silently missing boards is a review surface
// that covers less than it appears to -- the same shape as `make compare`'s
// default that skipped four screens and the card probe answered from cache. A
// check that reports on less than it claims is worse than no check, because it
// is trusted.
//
// HOW THIS WAS RECONSTRUCTED, since the original is gone and this is not it. The
// only specification left was the 3 MB output, whose own README comment names the
// appifact-doc block as the editable state. payload.template.html is that output
// with its doc line and its two title sites replaced by placeholders -- lossless,
// because those are the only variable parts -- and the encoding was read off the
// committed payload rather than guessed (sorted keys; `<` and nothing else;
// literal UTF-8; title/content.files/comments). test_seed_canvas.mjs then rebuilds
// the committed canvas from the content that canvas carries and asserts BYTE
// IDENTITY. It reproduces it exactly, which is the only available evidence that
// this is *the* generator rather than one that merely looks right -- the canvas
// editor on the other end expects a specific payload shape, and a near miss would
// be a 3 MB page that silently fails to load somebody's design.
//
// Usage (and see `make canvas` / `make canvas-check`, which are the supported
// entry points -- prefer those to retyping this):
//
//   node tools/design-canvas/seed-canvas.mjs \
//     --template tools/design-canvas/payload.template.html \
//     --out design/ereader-v1-ui.html --title "Encre UI" \
//     --canvas design/canvas.json --boards-dir design
//
//   --boards-dir DIR  seed every *.dc.html in DIR. PREFERRED: a list the caller
//                     maintains is a list a board can be left off, which is how
//                     the canvas went stale.
//   --artboard FILE   name one board; repeatable. The form the skill documented.
//                     The list must still cover --boards-dir (or the artboards'
//                     own directory), or this refuses.
//   --check           verify --out is byte-identical to what would be written,
//                     and write nothing. Exit 1 and say what drifted otherwise.

import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import { pathToFileURL } from "node:url";
import path from "node:path";

const DOC_TOKEN = "@@ENCRE_CANVAS_DOC@@";
const TITLE_TOKEN = "@@ENCRE_CANVAS_TITLE@@";

// The canvas grid, read off the committed layout: 480x800 boards on a 580/900
// pitch, row-major, eight columns to a row. Only used to SUGGEST a free slot in
// the error below -- this tool never places a board itself, because which page a
// board belongs on is a design decision and canvas.json is hand-maintained.
const GRID = { dx: 580, dy: 900, cols: 8 };

function die(msg) {
  process.stderr.write(`seed-canvas: ${msg}\n`);
  process.exit(1);
}

function parseArgs(argv) {
  const out = { artboards: [], check: false };
  const single = {
    "--template": "template", "--out": "out",
    "--title": "title", "--canvas": "canvas",
    "--boards-dir": "boardsDir",
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--check") { out.check = true; continue; }
    if (a === "--artboard") {
      const v = argv[++i];
      if (v === undefined) die("--artboard needs a path");
      out.artboards.push(v);
      continue;
    }
    if (a in single) {
      const v = argv[++i];
      if (v === undefined) die(`${a} needs a value`);
      // Deliberately NOT append-and-keep-the-last: argparse's silent overwrite
      // is what let `--only home --only library` drop `home` and report a
      // confident 1/1 in compare-design.py. Repeating a single-valued flag here
      // is an authoring mistake, so it is an error.
      if (out[single[a]] !== undefined) die(`${a} given more than once`);
      out[single[a]] = v;
      continue;
    }
    die(`unknown argument ${a}`);
  }
  for (const [flag, key] of Object.entries(single)) {
    if (key === "boardsDir") continue; // optional; derived below
    if (out[key] === undefined) die(`${flag} is required`);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Serialising the doc block.
//
// `<` MUST be escaped and nothing else needs to be. The JSON sits inside a
// <script> element, so a literal `</script>` anywhere in a board's source would
// close the block early and truncate the canvas at that byte -- and every board
// is HTML, so `<` is on almost every line (6,435 of them in the committed file).
// Escaping `<` alone makes that impossible while leaving the payload readable.
// `>` is left literal, non-ASCII is left literal as UTF-8, and `"`/`\`/control
// characters take JSON.stringify's own escapes. This reproduces the committed
// canvas byte for byte, which is the check that says it is right.
export function serialiseDoc(title, files) {
  const ordered = {};
  // Sorted by name, so a reseed is a stable diff rather than a reshuffle keyed
  // on the order the boards happened to be passed in.
  for (const name of Object.keys(files).sort()) ordered[name] = files[name];
  const doc = { title, content: { files: ordered }, comments: [] };
  return JSON.stringify(doc).replace(/</g, "\\u003c");
}

function htmlEscape(s) {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;")
          .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

// The pure half: template + title + files -> the page. EXPORTED so that
// test_seed_canvas.mjs can drive it directly and assert byte-identity against
// the committed canvas, which was seeded before the layout checks above existed
// and so cannot pass them. That test is the only thing that says this file is
// THE generator rather than merely A generator, and it must not be bought with
// an escape-hatch flag on the CLI -- a flag that skips a check is how the check
// stops running. The CLI applies every check, unconditionally, always.
export function assemble(template, title, files) {
  const docs = template.split(DOC_TOKEN);
  if (docs.length !== 2) {
    throw new Error(`template must contain ${DOC_TOKEN} exactly once (found `
      + `${docs.length - 1}); it is not a canvas payload template`);
  }
  if (!template.includes(TITLE_TOKEN)) {
    throw new Error(`template must contain ${TITLE_TOKEN}; it is not a canvas `
      + `payload template`);
  }
  const html = template
    .split(TITLE_TOKEN).join(htmlEscape(title))
    .split(DOC_TOKEN).join(serialiseDoc(title, files));

  // A truncated 3 MB file is exactly the thing nobody reads to the end of.
  if (html.includes(DOC_TOKEN) || html.includes(TITLE_TOKEN)) {
    throw new Error("a placeholder survived substitution");
  }
  if (!html.endsWith("</html>\n")) {
    throw new Error("the assembled page does not end with </html>, so the "
      + "template is truncated");
  }
  return html;
}

// Pull the doc block back out of an assembled page. Used by --check to say WHAT
// drifted, and by the test.
export function extractDoc(html) {
  const open = '<script type="application/json" id="appifact-doc">\n';
  const at = html.indexOf(open);
  if (at < 0) return null;
  const from = at + open.length;
  const to = html.indexOf("\n", from);
  if (to < 0) return null;
  try {
    return JSON.parse(html.slice(from, to));
  } catch {
    return null;
  }
}

function build(args) {
  // ---- collect the artboards -------------------------------------------
  // --boards-dir IS THE PREFERRED FORM AND IT IS NOT SUGAR. A board list the
  // caller maintains is a list the caller can leave a board off -- which is
  // exactly how the canvas went stale -- so deriving it from the directory makes
  // the list complete by construction rather than by a check. `make canvas` uses
  // it. --artboard is kept because the design-change skill documents it and
  // because a deliberate subset is occasionally wanted; when it is used, CHECK 1
  // below verifies the list covers the directory anyway.
  if (args.artboards.length === 0) {
    if (args.boardsDir === undefined) {
      // Seeding zero boards would produce a canvas with nothing on it and exit 0
      // -- the quiet pass this whole tool is a reaction to.
      die("no --artboard and no --boards-dir, so the canvas would have no boards on it");
    }
    let found;
    try {
      found = readdirSync(args.boardsDir).filter((f) => f.endsWith(".dc.html")).sort();
    } catch (e) {
      die(`cannot list boards in ${args.boardsDir}: ${e.message}`);
    }
    if (found.length === 0) die(`no .dc.html boards found in ${args.boardsDir}`);
    args.artboards = found.map((f) => path.join(args.boardsDir, f));
  }

  const files = {};
  const seen = new Map();
  for (const p of args.artboards) {
    const name = path.basename(p);
    if (!name.endsWith(".dc.html")) {
      die(`--artboard ${p} is not a .dc.html board`);
    }
    if (seen.has(name)) {
      // The same board twice would collapse into one key silently. compare-design.py
      // errors on a repeated screen id for this reason and does not de-duplicate:
      // two rows naming one id is a real question about which was meant.
      die(`board ${name} was given twice (as ${seen.get(name)} and ${p})`);
    }
    seen.set(name, p);
    try {
      files[name] = readFileSync(p, "utf8");
    } catch (e) {
      die(`cannot read board ${p}: ${e.message}`);
    }
  }

  // ---- CHECK 1: every board on disk is on the canvas --------------------
  // The omission this closes: a board authored design-first and never added to
  // the reseed. It was six boards behind when #60 was filed and nothing said so.
  const boardsDir = args.boardsDir ?? path.dirname(args.artboards[0]);
  let onDisk;
  try {
    onDisk = readdirSync(boardsDir).filter((f) => f.endsWith(".dc.html")).sort();
  } catch (e) {
    die(`cannot list boards in ${boardsDir}: ${e.message}`);
  }
  if (onDisk.length === 0) die(`no .dc.html boards found in ${boardsDir}`);
  const unseeded = onDisk.filter((f) => !(f in files));
  if (unseeded.length) {
    die(`${unseeded.length} board(s) exist in ${boardsDir} and were not passed as `
      + `--artboard, so the canvas would not carry them -- pass every board, or `
      + `delete the ones that are gone:\n`
      + unseeded.map((f) => `  ${f}`).join("\n"));
  }

  // ---- the layout ------------------------------------------------------
  let canvasRaw, canvas;
  try {
    canvasRaw = readFileSync(args.canvas, "utf8");
  } catch (e) {
    die(`cannot read --canvas ${args.canvas}: ${e.message}`);
  }
  try {
    canvas = JSON.parse(canvasRaw);
  } catch (e) {
    die(`--canvas ${args.canvas} is not valid JSON: ${e.message}`);
  }
  if (!Array.isArray(canvas.artboards)) {
    die(`--canvas ${args.canvas} has no "artboards" array`);
  }
  files[path.basename(args.canvas)] = canvasRaw;

  // ---- CHECK 2: every board has a place on the canvas -------------------
  // BEING IN THE FILES RECORD IS NOT BEING ON THE CANVAS. A board with no
  // artboard entry is loaded and never placed, so it is invisible in review --
  // a second, quieter way to cover less than the canvas claims, and the axis
  // that was actually six boards behind.
  const placed = new Set(canvas.artboards.map((a) => a.file));
  const unplaced = onDisk.filter((f) => !placed.has(f));
  if (unplaced.length) {
    // Suggest the next free row-major slot per page so the fix is mechanical,
    // but do not APPLY it: which page a board belongs on is editorial.
    const pages = {};
    for (const a of canvas.artboards) {
      pages[a.page] = Math.max(pages[a.page] ?? -1,
        Math.round(a.y / GRID.dy) * GRID.cols + Math.round(a.x / GRID.dx));
    }
    const hint = Object.entries(pages).map(([pid, last]) => {
      const n = last + 1;
      return `  ${pid}: next free slot is x=${(n % GRID.cols) * GRID.dx}, `
           + `y=${Math.floor(n / GRID.cols) * GRID.dy}`;
    }).join("\n");
    die(`${unplaced.length} board(s) have no artboard entry in ${args.canvas}, so `
      + `the canvas would load them and never show them:\n`
      + unplaced.map((f) => `  ${f}`).join("\n")
      + `\nAdd an entry {file, title, x, y, w, h, page} for each.\n${hint}`);
  }

  // ---- CHECK 3: the mirror -- no layout entry for a board that is gone --
  // compare-design.py's absent-board guard, one file over: an entry naming a
  // deleted board leaves an empty frame on the canvas, and deleting a board
  // would otherwise make the canvas quietly wrong rather than loudly incomplete.
  const orphans = canvas.artboards.map((a) => a.file).filter((f) => !(f in files));
  if (orphans.length) {
    die(`${orphans.length} artboard entr(ies) in ${args.canvas} name a board that `
      + `does not exist, which would draw an empty frame -- remove the entry:\n`
      + [...new Set(orphans)].map((f) => `  ${f}`).join("\n"));
  }

  // ---- assemble ---------------------------------------------------------
  let template;
  try {
    template = readFileSync(args.template, "utf8");
  } catch (e) {
    die(`cannot read --template ${args.template}: ${e.message}`);
  }
  let html;
  try {
    html = assemble(template, args.title, files);
  } catch (e) {
    die(e.message);
  }
  return { html, boards: onDisk.length };
}

function cli(argv) {
  const args = parseArgs(argv);
  const { html, boards } = build(args);

  if (!args.check) {
    writeFileSync(args.out, html);
    process.stdout.write(`wrote ${args.out}: ${boards} boards, ${html.length} chars\n`);
    return;
  }

  let have;
  try {
    have = readFileSync(args.out, "utf8");
  } catch (e) {
    die(`--check but ${args.out} cannot be read: ${e.message}`);
  }
  if (have === html) {
    process.stdout.write(`canvas current: ${args.out}, ${boards} boards\n`);
    return;
  }

  // Say WHAT drifted, not just that something did. The doc is ONE 526 KB line,
  // so `git diff` on this file is a single unreadable "1 insertion, 1 deletion"
  // -- every canvas commit in this repo's history looks identical in a diffstat.
  // A verdict of "stale" with no noun attached is not actionable either.
  const mine = extractDoc(html);
  const theirs = extractDoc(have);
  const lines = [];
  if (theirs === null) {
    lines.push("  the committed canvas has no readable appifact-doc block");
  } else {
    const a = Object.keys(mine.content.files);
    const b = Object.keys(theirs.content.files);
    const missing = a.filter((f) => !b.includes(f));
    const stale = b.filter((f) => !a.includes(f));
    const changed = a.filter((f) => b.includes(f)
      && mine.content.files[f] !== theirs.content.files[f]);
    if (missing.length) {
      lines.push(`  ${missing.length} board(s) on disk and absent from the canvas: `
        + missing.join(", "));
    }
    if (stale.length) {
      lines.push(`  ${stale.length} file(s) on the canvas and no longer on disk: `
        + stale.join(", "));
    }
    if (changed.length) {
      lines.push(`  ${changed.length} board(s) edited since the last reseed: `
        + changed.join(", "));
    }
    if (mine.title !== theirs.title) {
      lines.push(`  title: ${JSON.stringify(theirs.title)} -> ${JSON.stringify(mine.title)}`);
    }
    if (!lines.length) {
      lines.push("  the content matches, so the editor bundle in --template "
        + "differs from the committed page");
    }
  }
  die(`${args.out} is not what the boards say it should be:\n`
    + lines.join("\n") + "\nReseed it with: make canvas");
}

// Run the CLI only when this file IS the program. Importing it (the test does)
// must not execute anything.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  cli(process.argv.slice(2));
}

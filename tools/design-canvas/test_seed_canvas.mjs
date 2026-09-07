#!/usr/bin/env node
// The generator's own test. Plain `node`, no framework, no dependency.
//
// DELIBERATELY NOT WIRED INTO `make test`, for tools/test_compare_design.py's
// reason: the fast loop builds core/ on a bare checkout with no interpreter and
// no submodule, and a node dependency there would be worse than a Python one.
// It runs from `make canvas-test`.
//
// THE FIRST CASE IS THE ONE THAT MATTERS: rebuild the committed canvas from the
// content the committed canvas itself carries, and demand byte-identity. That is
// what says this file is THE generator and not merely A generator -- the
// original seed-canvas.mjs was lost (#60), so the only specification left was
// its 3 MB output, and reproducing that output exactly is the whole proof.
//
// It drives assemble() rather than the CLI, because the committed canvas predates
// the CLI's layout checks and cannot satisfy them -- five of the boards it
// carries have no artboard entry, which is the defect it was filed for. Testing
// through a flag that switched those checks off would mean shipping that flag.

import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { assemble, serialiseDoc, extractDoc } from "./seed-canvas.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..", "..");

let failed = 0;
function check(name, cond, detail = "") {
  if (cond) {
    process.stdout.write(`  ok   ${name}\n`);
  } else {
    failed++;
    process.stdout.write(`  FAIL ${name}${detail ? `\n       ${detail}` : ""}\n`);
  }
}

const template = readFileSync(
  path.join(HERE, "payload.template.html"), "utf8");
const committed = readFileSync(
  path.join(ROOT, "design", "ereader-v1-ui.html"), "utf8");

process.stdout.write("round trip against the committed canvas\n");

const doc = extractDoc(committed);
check("the committed canvas has a readable doc block", doc !== null);

if (doc) {
  const rebuilt = assemble(template, doc.title, doc.content.files);
  check(`rebuilding it from its own ${Object.keys(doc.content.files).length} `
      + `embedded files is byte-identical`,
    rebuilt === committed,
    `rebuilt ${rebuilt.length} chars, committed ${committed.length} chars`);

  // If that fails, the assembly is wrong somewhere specific. Narrow it.
  if (rebuilt !== committed) {
    const a = rebuilt.split("\n"), b = committed.split("\n");
    check("same line count", a.length === b.length,
      `${a.length} vs ${b.length}`);
    for (let i = 0; i < Math.min(a.length, b.length); i++) {
      if (a[i] !== b[i]) {
        process.stdout.write(`       first differing line ${i + 1} `
          + `(len ${a[i].length} vs ${b[i].length})\n`);
        for (let k = 0; k < Math.min(a[i].length, b[i].length); k++) {
          if (a[i][k] !== b[i][k]) {
            process.stdout.write(`       at char ${k}\n`);
            process.stdout.write(`        got: ${JSON.stringify(a[i].slice(Math.max(0, k - 50), k + 50))}\n`);
            process.stdout.write(`       want: ${JSON.stringify(b[i].slice(Math.max(0, k - 50), k + 50))}\n`);
            break;
          }
        }
        break;
      }
    }
  }
}

process.stdout.write("the doc block's encoding\n");

// `<` must be escaped -- a literal `</script>` in a board would close the block
// early and truncate the canvas at that byte. Nothing else needs escaping, and
// escaping more would not reproduce the committed file.
const s = serialiseDoc("T", { "a.dc.html": '<p x="1">é \\ </script>\n' });
check("`<` is escaped as \\u003c", !s.includes("<") && s.includes("\\u003c"));
check("`</script>` cannot survive", !s.includes("</script>"));
check("`>` is left literal", s.includes(">"));
check("non-ASCII is left literal UTF-8", s.includes("é"));
check("a quote is JSON-escaped", s.includes('\\"1\\"'));
check("a backslash is JSON-escaped", s.includes("\\\\"));
check("a newline is JSON-escaped", s.includes("\\n"));

check("the shape is title / content.files / comments",
  (() => {
    const d = JSON.parse(s.replace(/\\u003c/g, "<"));
    return Object.keys(d).join(",") === "title,content,comments"
      && Object.keys(d.content).join(",") === "files"
      && Array.isArray(d.comments) && d.comments.length === 0;
  })());

check("files are ordered by name whatever order they arrive in",
  (() => {
    const d = serialiseDoc("T", { "z.dc.html": "z", "a.dc.html": "a", "canvas.json": "c" });
    const doc2 = JSON.parse(d);
    return Object.keys(doc2.content.files).join(",")
      === "a.dc.html,canvas.json,z.dc.html";
  })());

check("a board's bytes are carried verbatim",
  (() => {
    const src = readFileSync(path.join(ROOT, "design", "Main.dc.html"), "utf8");
    const d = JSON.parse(serialiseDoc("T", { "Main.dc.html": src })
      .replace(/\\u003c/g, "<"));
    return d.content.files["Main.dc.html"] === src;
  })());

process.stdout.write("assemble() refuses a template it cannot fill\n");

function throws(fn) {
  try { fn(); return false; } catch { return true; }
}
check("a template with no doc placeholder is refused",
  throws(() => assemble("<html>@@ENCRE_CANVAS_TITLE@@</html>\n", "T", {})));
check("a template with no title placeholder is refused",
  throws(() => assemble("<html>@@ENCRE_CANVAS_DOC@@</html>\n", "T", {})));
check("a template with two doc placeholders is refused",
  throws(() => assemble(
    "@@ENCRE_CANVAS_TITLE@@@@ENCRE_CANVAS_DOC@@@@ENCRE_CANVAS_DOC@@</html>\n", "T", {})));
check("a truncated template is refused",
  throws(() => assemble("@@ENCRE_CANVAS_TITLE@@@@ENCRE_CANVAS_DOC@@", "T", {})));
check("a title is HTML-escaped into the head",
  assemble("<title>@@ENCRE_CANVAS_TITLE@@</title>@@ENCRE_CANVAS_DOC@@</html>\n",
    'A & B <x>', {}).includes("<title>A &amp; B &lt;x&gt;</title>"));

// ---------------------------------------------------------------------------
// The guards, driven through the CLI against throwaway trees.
//
// EVERY ONE OF THESE IS A CASE WHERE THE OLD ROUTE EXITED 0 ON A CANVAS THAT
// COVERED LESS THAN IT CLAIMED. Asserting the exit code is the point: a guard
// that has stopped firing looks exactly like a repository with nothing wrong.

process.stdout.write("the CLI's guards fire\n");

const SEED = path.join(HERE, "seed-canvas.mjs");
const TEMPLATE = path.join(HERE, "payload.template.html");

function run(dir, extra = []) {
  const r = spawnSync("node", [
    SEED, "--template", TEMPLATE,
    "--out", path.join(dir, "out.html"), "--title", "T",
    "--canvas", path.join(dir, "canvas.json"),
    ...extra,
  ], { encoding: "utf8" });
  return { code: r.status, err: r.stderr ?? "" };
}

function tree(boards, artboards) {
  const dir = mkdtempSync(path.join(tmpdir(), "seedcanvas-"));
  for (const b of boards) writeFileSync(path.join(dir, b), `<p>${b}</p>\n`);
  writeFileSync(path.join(dir, "canvas.json"), JSON.stringify({
    artboards: artboards.map((f, i) => ({
      file: f, title: f, x: i * 580, y: 0, w: 480, h: 800, page: "page-1",
    })),
    annotations: [], pages: [{ id: "page-1", name: "P" }],
    launch: { view: "canvas", page: "page-1" },
  }, null, 2) + "\n");
  return dir;
}

const trees = [];
function t(boards, artboards) {
  const d = tree(boards, artboards);
  trees.push(d);
  return d;
}

// The happy path, so a guard case failing cannot be the harness being broken.
{
  const d = t(["A.dc.html", "B.dc.html"], ["A.dc.html", "B.dc.html"]);
  const r = run(d, ["--boards-dir", d]);
  check("a complete, fully placed set seeds", r.code === 0, r.err.trim());
  check("and the output is a whole page",
    readFileSync(path.join(d, "out.html"), "utf8").endsWith("</html>\n"));
}

// CHECK 1 -- a board on disk left off a hand-written --artboard list. This is
// the omission the ticket was filed for: the canvas went boards behind and
// nothing said so.
{
  const d = t(["A.dc.html", "B.dc.html"], ["A.dc.html", "B.dc.html"]);
  const r = run(d, ["--artboard", path.join(d, "A.dc.html")]);
  check("a board on disk and off the --artboard list is refused", r.code === 1);
  check("...and it is NAMED", r.err.includes("B.dc.html"), r.err.trim());
}

// CHECK 2 -- carried but unplaced. Being in the files record is NOT being on the
// canvas: with no artboard entry the editor loads a board and never shows it,
// which is invisible in review. Five of the boards the committed canvas carries
// were in this state.
{
  const d = t(["A.dc.html", "B.dc.html"], ["A.dc.html"]);
  const r = run(d, ["--boards-dir", d]);
  check("a board with no artboard entry is refused", r.code === 1);
  check("...and it is NAMED", r.err.includes("B.dc.html"), r.err.trim());
  check("...and a free grid slot is suggested", r.err.includes("next free slot"));
}

// CHECK 3 -- the mirror: a layout entry for a board that is gone, which draws an
// empty frame. compare-design.py's absent-board guard, one file over.
{
  const d = t(["A.dc.html"], ["A.dc.html", "Ghost.dc.html"]);
  const r = run(d, ["--boards-dir", d]);
  check("a layout entry naming no board is refused", r.code === 1);
  check("...and it is NAMED", r.err.includes("Ghost.dc.html"), r.err.trim());
}

// Seeding nothing would produce an empty canvas and exit 0.
{
  const d = t(["A.dc.html"], ["A.dc.html"]);
  const r = run(d, []);
  check("seeding no boards at all is refused", r.code === 1, r.err.trim());
}
{
  const d = t([], []);
  const r = run(d, ["--boards-dir", d]);
  check("an empty boards directory is refused", r.code === 1, r.err.trim());
}

// One board twice would collapse to one key silently.
{
  const d = t(["A.dc.html"], ["A.dc.html"]);
  const a = path.join(d, "A.dc.html");
  const r = run(d, ["--artboard", a, "--artboard", a]);
  check("the same board twice is refused", r.code === 1, r.err.trim());
}

// A repeated single-valued flag: argparse's silent overwrite is what let
// `--only home --only library` drop `home` in compare-design.py.
{
  const d = t(["A.dc.html"], ["A.dc.html"]);
  const r = run(d, ["--boards-dir", d, "--title", "again"]);
  check("a single-valued flag given twice is refused", r.code === 1, r.err.trim());
}

// --check must FAIL on a stale file, not shrug.
{
  const d = t(["A.dc.html", "B.dc.html"], ["A.dc.html", "B.dc.html"]);
  check("--check passes on a canvas it just wrote",
    run(d, ["--boards-dir", d]).code === 0
      && run(d, ["--boards-dir", d, "--check"]).code === 0);
  writeFileSync(path.join(d, "B.dc.html"), "<p>edited</p>\n");
  const r = run(d, ["--boards-dir", d, "--check"]);
  check("--check fails once a board is edited", r.code === 1);
  check("...and names the edited board",
    r.err.includes("B.dc.html") && r.err.includes("edited since"), r.err.trim());
}

for (const d of trees) rmSync(d, { recursive: true, force: true });

process.stdout.write(failed
  ? `\n${failed} failure(s)\n`
  : "\nall assertions passed\n");
process.exit(failed ? 1 : 0);

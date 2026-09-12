#!/usr/bin/env python3
"""Tests for compare-design.py's SCREEN SELECTION, which is the half that can
report on less than it claims.

    python3 tools/test_compare_design.py

DELIBERATELY NOT WIRED INTO `make test`. That target runs on a bare checkout
with no Python and no submodule -- every generated asset is committed precisely
so it can -- and making the fast loop depend on an interpreter would be a worse
trade than these tests are worth. They are the tests for a tool, run when the
tool changes.

WHAT THEY COVER AND WHY IT IS THIS AND NOT PIXELS: Chrome and the simulator are
stubbed out, so nothing here renders anything. What is left is the part with the
history -- which screens a run decided to compare, and whether the count it
prints is over that set or a subset of it. Five separate defects in this repo
have been that shape (CLAUDE.md records them under "The rule that governs UI
work"): the default that compared seven boards of thirty-odd, a board named in
the list and absent from disk that shrank the DENOMINATOR, `render_sim`
returning a bare None for two opposite failures, `--only <flow screen>` matching
nothing at all, and a repeated `--only` whose earlier ids were silently dropped.
Every one of them printed a confident ratio. So the assertions here are about
the SET and the COUNT, never about a pixel.

AND NOW ABOUT THE MISMATCH FIGURE, which is the same family one number over: it
is an ARITHMETIC over two panels, and the assertions below are over synthetic
images whose answer is known by construction -- a 128 boundary, a half-black
panel -- never over a rendered screen. Pinning a real render here would be a
golden in the wrong file. What these cases defend is the boundary (`<= 128`
moves `contents` off its recorded 8,814 pixels by one), the denominator, the
figure being per PANEL, and its absence beside a screen the firmware does not
draw.
"""
import contextlib
import importlib.util
import io
import pathlib
import sys
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent


def load():
    """Import compare-design.py, whose hyphen keeps it off the import path."""
    spec = importlib.util.spec_from_file_location(
        "compare_design", ROOT / "tools" / "compare-design.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def fake_render(path):
    """The image a stubbed Chrome or simulator "produced", by output filename.

    REAL PILLOW IMAGES, and small ones: normalise() resizes whatever it is given
    to the panel size with NEAREST, so a 2x1 source becomes exact half-panel
    columns at both geometries (480 and 528 are both even). The design side is
    all paper and the firmware side is half ink, which makes the mismatch figure
    EXACTLY 50% of the panel -- a number a test can assert without rendering
    anything, and one that differs in PIXELS between the two geometries, which
    is what pins the figure as per-panel rather than per-screen.

    `save` therefore really writes, so the export test can count FILES rather
    than trusting the tool's own tally -- which is the whole point of that case.
    """
    im = Image.new("L", (2, 1), 255)
    if "_sim" in pathlib.Path(path).name:
        im.putpixel((0, 0), 0)
    return im


class Run:
    def __init__(self, code, message, screens, rows, stdout):
        self.code = code            # 0, or the exit status/message
        self.message = message      # SystemExit's string, or ""
        self.screens = screens      # ids handed to the simulator, in order
        self.rows = rows            # what compose() was given
        self.stdout = stdout

    @property
    def total(self):
        """The DENOMINATOR the sheet prints. Not len(self.screens): a screen is
        rendered once per geometry, and a row is one screen."""
        return len(self.rows)

    @property
    def done(self):
        return sum(1 for _, _, impl in self.rows if impl)

    @property
    def ids(self):
        """Distinct ids, in first-seen order."""
        out = []
        for sid in self.screens:
            if sid not in out:
                out.append(sid)
        return out


def run(mod, argv, sim_status=None, chrome="/bin/echo"):
    """Drive main() with Chrome, the simulator and Pillow stubbed out.

    `sim_status` maps a screen id to the status render_sim should report, so a
    test can make one screen of several fail. Default: everything renders.
    """
    sim_status = sim_status or {}
    rendered = []
    captured = {}

    mod.CHROME = chrome
    mod.render_board = lambda bp, out, w, h: pathlib.Path(out)

    def fake_sim(sid, out, w, h):
        rendered.append(sid)
        status = sim_status.get(sid, "ok")
        return (pathlib.Path(out) if status == "ok" else None), status

    mod.render_sim = fake_sim
    # normalise() and mismatch() are NOT stubbed: they are cheap, and the figure
    # the run prints is the thing under test in the cases below. placeholder is,
    # and it returns a real PAPER panel -- so a mutation that measured one would
    # print a plausible "0.00%" beside an unimplemented screen rather than
    # crashing, which is the failure a test has to be able to see.
    mod.placeholder = lambda text, w, h: Image.new("L", (w, h), 245)
    mod.Image.open = lambda p: fake_render(p)

    def fake_compose(rows, out, geom_keys, pairs_per_row=2):
        captured["rows"] = list(rows)
        return (0, 0), sum(1 for r in rows if r[2]), len(rows)

    mod.compose = fake_compose

    old_argv = sys.argv
    sys.argv = ["compare-design.py"] + list(argv)
    buf = io.StringIO()
    code, message = 0, ""
    try:
        with contextlib.redirect_stdout(buf):
            mod.main()
    except SystemExit as e:
        if isinstance(e.code, int) or e.code is None:
            code, message = e.code or 0, ""
        else:
            code, message = 1, str(e.code)
    finally:
        sys.argv = old_argv
    return Run(code, message, rendered, captured.get("rows", []), buf.getvalue())


FAILURES = []


def check(ok, what, detail=""):
    print(("  ok   " if ok else "  FAIL ") + what + (f"   [{detail}]" if detail and not ok else ""))
    if not ok:
        FAILURES.append(what)


def main():
    mod = load()
    x4 = ["--geometry", "x4"]

    print("--only accepts a list, in both spellings")
    # A COMMA-SEPARATED LIST is the documented spelling -- the module docstring
    # and CLAUDE.md both show `--only home,reader` -- and it is the one that
    # survives $(COMPARE_ARGS) being expanded unquoted by make.
    r = run(mod, ["--only", "home,library"] + x4)
    check(r.code == 0 and r.ids == ["home", "library"] and r.total == 2,
          "--only home,library compares two screens", f"exit={r.code} ids={r.ids} total={r.total}")

    # A REPEATED FLAG must ACCUMULATE, not let the last one win. argparse's
    # default is to overwrite, which silently dropped every id but the last and
    # still printed a confident "1/1 screens implemented".
    r = run(mod, ["--only", "home", "--only", "library"] + x4)
    check(r.code == 0 and r.ids == ["home", "library"] and r.total == 2,
          "--only home --only library compares two screens", f"exit={r.code} ids={r.ids} total={r.total}")

    # And the two spellings mix, because a user who learns one will combine them.
    r = run(mod, ["--only", "home,library", "--only", "settings"] + x4)
    check(r.code == 0 and set(r.ids) == {"home", "library", "settings"} and r.total == 3,
          "--only home,library --only settings compares three screens",
          f"exit={r.code} ids={r.ids} total={r.total}")

    print("\nan unrecognised id is a hard error WHEREVER it sits")
    # The precedent is exact: --only was already made to error rather than
    # report "0/0 implemented", which reads like a pass. Every element gets it,
    # so position cannot decide whether a typo is caught.
    for argv, where in ((["--only", "home,nosuchscreen"], "last, comma form"),
                        (["--only", "nosuchscreen,home"], "first, comma form"),
                        (["--only", "home", "--only", "nosuchscreen"], "last, repeated flag"),
                        (["--only", "nosuchscreen", "--only", "home"], "first, repeated flag")):
        r = run(mod, argv + x4)
        check(r.code != 0 and "nosuchscreen" in r.message,
              f"unrecognised id rejected ({where})", f"exit={r.code} msg={r.message!r}")

    # Naming several typos names them all, rather than stopping at the first.
    r = run(mod, ["--only", "nope1,home,nope2"] + x4)
    check(r.code != 0 and "nope1" in r.message and "nope2" in r.message,
          "every unrecognised id is named", f"msg={r.message!r}")

    # AN EMPTY --only IS THE SAME QUIET PASS REACHED FROM THE OTHER SIDE: it
    # selects nothing, so nothing is MISSING either, and the run exits 0 having
    # rendered no screens and reported "0/0 screens implemented".
    for argv, spelling in ((["--only", ""], 'empty string'),
                           (["--only", ","], 'a bare comma'),
                           (["--only", " , "], 'whitespace and a comma')):
        r = run(mod, argv + x4)
        check(r.code != 0 and r.total == 0,
              f"an empty --only is an error, not 0/0 ({spelling})",
              f"exit={r.code} total={r.total} msg={r.message!r}")

    print("\nthe screen tables are a set, not a bag")
    # A DUPLICATE ROW INFLATES BOTH HALVES OF THE RATIO. `reader_anchored` was
    # listed in V1_SCREENS and in FLOW_SCREENS at once, so the default sheet
    # rendered it twice, counted it twice, and printed a denominator one larger
    # than the number of screens that exist. Same family as the absent board
    # that shrank the denominator: the count has to be over the set it claims.
    tables = {"V1_SCREENS": mod.V1_SCREENS, "FLOW_SCREENS": mod.FLOW_SCREENS,
              "V2_SCREENS": mod.V2_SCREENS}
    every = [row for t in tables.values() for row in t]
    ids = [sid for sid, _, _ in every]
    dupes = sorted({sid for sid in ids if ids.count(sid) > 1})
    check(not dupes, "no screen id appears in more than one table row", f"duplicated: {dupes}")

    boards = [b for _, b, _ in every]
    dupe_boards = sorted({b for b in boards if boards.count(b) > 1})
    check(not dupe_boards, "no design board is named by two rows", f"duplicated: {dupe_boards}")

    # And the guard is structural, not just this assertion: a duplicate added
    # later must stop a RUN, not only this file.
    saved = mod.FLOW_SCREENS
    try:
        mod.FLOW_SCREENS = saved + [mod.V1_SCREENS[0]]
        r = run(mod, x4)
        check(r.code != 0 and mod.V1_SCREENS[0][0] in r.message,
              "a duplicated row makes a run fail rather than double-count",
              f"exit={r.code} total={r.total} msg={r.message!r}")
    finally:
        mod.FLOW_SCREENS = saved

    print("\nthe parked V2 boards stay reachable and stay uncounted")
    # V2_SCREENS keeps the cut Wi-Fi/Transfer boards nameable without counting
    # them as V1 work nobody is doing.
    r = run(mod, x4)
    default_ids = set(r.ids)
    check(r.code == 0 and not (default_ids & {s[0] for s in mod.V2_SCREENS}),
          "a default run compares no V2 board")
    # DISTINCT ids, not len(V1) + len(FLOW): comparing against the row count
    # would be satisfied by the duplicate row that inflated it, which is how an
    # assertion ends up agreeing with the defect it was written to catch.
    distinct_default = {s[0] for s in mod.V1_SCREENS + mod.FLOW_SCREENS}
    check(r.total == len(distinct_default) == len(r.ids),
          "a default run's denominator is every V1 and flow SCREEN, once each",
          f"total={r.total} distinct={len(distinct_default)} rendered={len(r.ids)}")

    r = run(mod, ["--only", "transfer,wifi_picker"] + x4)
    check(r.code == 0 and set(r.ids) == {"transfer", "wifi_picker"} and r.total == 2,
          "--only reaches several V2 boards at once", f"exit={r.code} ids={r.ids}")

    # A MIXED SELECTION is the case worth pinning: a V1 id and a parked one in
    # one run must both render, and both must be counted, since the count is
    # over what was asked for rather than over what is V1.
    r = run(mod, ["--only", "home,transfer"] + x4)
    check(r.code == 0 and set(r.ids) == {"home", "transfer"} and r.total == 2,
          "--only mixes a V1 and a parked board in one run", f"exit={r.code} ids={r.ids}")

    print("\n--require-implemented sees every screen of a multi-id run")
    # The gate must not be satisfied by the ids that happened to work. A screen
    # the simulator KNOWS and cannot draw is the failure it exists for, and it
    # is a failure wherever in the selection it sits.
    for bad, where in (("library", "second of two"), ("home", "first of two")):
        r = run(mod, ["--only", "home,library", "--require-implemented"] + x4,
                sim_status={bad: "failed"})
        check(r.code != 0, f"--require-implemented fails on a broken screen ({where})",
              f"exit={r.code}")

    # A board with no screen behind it is still fine -- that is a third of the
    # list -- so an unimplemented screen must NOT trip the gate.
    r = run(mod, ["--only", "home,library", "--require-implemented"] + x4,
            sim_status={"library": "unimplemented"})
    check(r.code == 0 and r.done == 1 and r.total == 2,
          "--require-implemented passes an unimplemented screen",
          f"exit={r.code} done={r.done}/{r.total}")

    print("\n--export writes one file per render, and says how many it wrote")
    # The tally is checked against the DIRECTORY, not against itself: a
    # duplicated row used to write one file twice and count it twice, so the
    # line over-reported against what a design tool would find there.
    with tempfile.TemporaryDirectory() as tmp:
        r = run(mod, ["--only", "home,library", "--export", tmp] + x4)
        files = sorted(p.name for p in pathlib.Path(tmp).iterdir())
        claimed = [ln for ln in r.stdout.splitlines() if "bare panel PNGs" in ln]
        check(r.code == 0 and files == ["home_x4_design.png", "home_x4_firmware.png",
                                        "library_x4_design.png", "library_x4_firmware.png"],
              "--export writes both sides of every selected screen", f"files={files}")
        check(claimed and claimed[0].startswith(f"exported {len(files)} "),
              "the exported count matches the files on disk",
              f"files={len(files)} line={claimed!r}")

    print("\nthe mismatch figure is threshold-at-128, over the panel's own pixels")

    def panel(values, w=None):
        """A 1-row image from a list of greys."""
        im = Image.new("L", (len(values) if w is None else w, 1), 255)
        for x, v in enumerate(values):
            im.putpixel((x, 0), v)
        return im

    # IDENTICAL PANELS ARE 0 OF w*h. The denominator is the panel, not the
    # differing count and not the ink: `sleep_cover` reads a true 0.00% because
    # the board displays the firmware's own committed PNG, and that has to stay
    # expressible.
    a = Image.new("L", (7, 5), 255)
    check(mod.mismatch(a, a.copy()) == (0, 35),
          "identical panels are 0 differing of w*h", f"{mod.mismatch(a, a.copy())}")

    b = Image.new("L", (7, 5), 0)
    check(mod.mismatch(a, b) == (35, 35),
          "opposite panels are every pixel", f"{mod.mismatch(a, b)}")

    # THE BOUNDARY. Ink is BELOW 128, so 127 is ink and 128 is paper. `<= 128`
    # is the mutation this case exists for: it is invisible on most screens and
    # moves `contents` off its recorded 8,814 differing pixels by exactly one.
    # Grey 127 being ink is also the mechanism behind the half-pixel phase
    # artefact CLAUDE.md records, where Chrome lays a 1px rule across two rows
    # of 127 and the count scores both against the firmware's one.
    paper = panel([255, 255, 255, 255])
    check(mod.mismatch(panel([126, 127, 128, 129]), paper)[0] == 2,
          "127 counts as ink and 128 as paper",
          f"{mod.mismatch(panel([126, 127, 128, 129]), paper)}")
    # And symmetrically: a panel that agrees about every pixel's SIDE of the
    # threshold is 0, however far apart the greys are. That is what makes this a
    # comparison of ink and not of tone.
    check(mod.mismatch(panel([0, 60, 127, 200]), panel([120, 0, 5, 255]))[0] == 0,
          "greys on the same side of 128 do not differ",
          f"{mod.mismatch(panel([0, 60, 127, 200]), panel([120, 0, 5, 255]))}")

    print("\nthe figure names the panel's grey levels, off the FIRMWARE render")
    # A threshold-at-128 count over four levels inflates against a one-bit
    # screen's, so `reader` at 5.24% is not worse than `reader_menu` at 3.00%.
    # The tag is measured off the render rather than read from a table of which
    # screens declare Fidelity::Grayscale -- that fact lives in core/.
    check(mod.panel_levels(panel([0, 255, 0, 255])) == 2, "a one-bit panel is 2 levels")
    check(mod.panel_levels(panel([0, 85, 170, 255])) == 4, "a grayscale panel is 4 levels")
    # The DESIGN panel is Chrome's antialiasing and has many levels on every
    # screen, so taking the tag from it would label the whole sheet 4-level.
    grey4 = panel([0, 85, 170, 255])
    check("1-bit" in mod.figure_for(panel([0, 60, 127, 200]), panel([0, 255, 0, 255])),
          "a one-bit firmware panel reads 1-bit beside an antialiased board")
    check("4-level" in mod.figure_for(panel([0, 60, 127, 200]), grey4),
          "a four-level firmware panel says so")

    # NO FIGURE WHERE THERE IS NOTHING HONEST TO MEASURE. A board with no screen
    # behind it is a third of this list, and a percentage against the NOT
    # IMPLEMENTED hatch would be a confident false claim.
    check(mod.figure_for(None, grey4) is None and mod.figure_for(grey4, None) is None,
          "a missing render has no figure rather than a number")

    print("\na run prints the figure per PANEL, and only where both sides rendered")
    r = run(mod, ["--only", "home,library"])
    lines = [ln for ln in r.stdout.splitlines() if "mismatch" in ln]
    # The stub makes every firmware panel exactly half ink against an all-paper
    # board, so the percentage is 50.00% by construction at both geometries.
    check(len(lines) == 4 and all("mismatch 50.00%" in ln for ln in lines),
          "every rendered panel carries its figure", f"lines={lines}")
    # PER PANEL, NOT PER SCREEN. The two geometries are the same half-panel and
    # different pixel counts, so a figure computed once and reused for both
    # would show 192000 twice.
    px = [ln.split("(")[1].split(" px")[0] for ln in lines]
    check(px == ["192000", "209088", "192000", "209088"],
          "each geometry is measured on its own pixels", f"px={px}")
    # The SHEET gets it too -- that PNG is the artifact CI uploads, and a figure
    # that lived only in a scrolled-away run log is one nobody reads later.
    figures = [entry[-1] for _, entries, _ in r.rows for entry in entries]
    check(all(isinstance(f, str) and "mismatch" in f for f in figures),
          "the sheet's geometry captions carry the figure", f"figures={figures}")

    r = run(mod, ["--only", "home,library"] + x4, sim_status={"library": "unimplemented"})
    named = [ln for ln in r.stdout.splitlines() if "not implemented" in ln]
    check(len(named) == 1 and "mismatch" not in named[0],
          "an unimplemented screen gets no figure", f"line={named}")

    print("\nthe board-vs-disk check covers every element of a selection")
    # A board named in the list and absent from disk is a hard error, and it has
    # to be one for the SECOND id as much as the first -- that defect once let a
    # deleted board shrink the denominator while the sheet still read N/M.
    saved = mod.FLOW_SCREENS
    try:
        mod.FLOW_SCREENS = saved + [("ghost_screen", "NoSuchBoard.dc.html", "Ghost")]
        for argv, where in ((["--only", "home,ghost_screen"], "second of two"),
                            (["--only", "ghost_screen,home"], "first of two")):
            r = run(mod, argv + x4)
            check(r.code != 0 and "NoSuchBoard.dc.html" in r.message,
                  f"an absent board fails the run ({where})", f"exit={r.code} msg={r.message!r}")
    finally:
        mod.FLOW_SCREENS = saved

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED:")
        for f in FAILURES:
            print(f"  - {f}")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

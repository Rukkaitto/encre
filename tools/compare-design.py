#!/usr/bin/env python3
"""Design-vs-firmware comparison sheet for every screen.

For each screen, rasterises its design board (a `.dc.html` from design/) in
headless Chrome at exact panel size and puts it beside the simulator's render
of the same screen, at 1:1 with no scaling. Screens the firmware does not draw
yet get a NOT IMPLEMENTED placeholder, so the sheet doubles as a progress
dashboard: run it and see how much of the design actually exists.

Every board is authored once, at the Xteink X4's 480x800 frame. The firmware
also targets the Xteink X3 (528x792), a different panel geometry entirely, so
each board is additionally re-rendered at the X3 frame by overriding the
board's root-element size at render time (see render_board) -- no board file
is ever edited. Each screen therefore produces one pair per geometry.

Not part of the build. Requires Google Chrome and Pillow.

    make test                                         # build the simulator
    python3 tools/compare-design.py                   # V1 screens, both geometries
    python3 tools/compare-design.py --all              # + flows and states
    python3 tools/compare-design.py --only home,reader
    python3 tools/compare-design.py --only home --only reader   # same run
    python3 tools/compare-design.py --geometry x3      # X3 (528x792) only

Its own tests are tools/test_compare_design.py, run with plain python3 and
deliberately not wired into `make test` -- which builds on a bare checkout with
no Python at all.

The design board is served over a throwaway localhost server because Chrome
does not load file:// subresources reliably.
"""
import argparse
import http.server
import os
import pathlib
import re
import shutil
import socketserver
import subprocess
import tempfile
import threading

from PIL import Image, ImageDraw

# The frame every design board is authored at (the literal inline-style size
# on each board's root element). render_board() overrides this at render
# time to produce the other geometry -- the board source is never touched.
AUTHORED_W, AUTHORED_H = 480, 800

# Device panel geometries to render and pair against the firmware. Order here
# is the left-to-right order in the output sheet.
GEOMETRIES = {
    "x4": {"device": "X4", "w": 480, "h": 800},
    "x3": {"device": "X3", "w": 528, "h": 792},
}
GEOMETRY_ORDER = ["x4", "x3"]

# Chrome rasterises the boards, and WHERE it lives depends on the machine. This
# was a hardcoded macOS path until CI wanted the same comparison on a Linux
# runner, where that path cannot exist -- so the run died at "Chrome not found"
# before rendering anything, which is a tooling fault reported as a design one.
# $CHROME wins if set, and is NOT checked against this list: a wrong override
# must report itself rather than fall through to a system Chrome that rasterises
# differently, because the whole point of this script is that the two engines
# agree.
CHROME_CANDIDATES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/usr/bin/google-chrome",
    "/usr/bin/google-chrome-stable",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
]


def resolve_chrome():
    """The Chrome binary to rasterise boards with, or None if there is none."""
    env = os.environ.get("CHROME")
    if env:
        return env
    for candidate in CHROME_CANDIDATES:
        if pathlib.Path(candidate).exists():
            return candidate
    return shutil.which("google-chrome") or shutil.which("chromium")


CHROME = resolve_chrome()

# Extra flags for the Chrome invocation, space-separated. A sandboxed CI runner
# usually needs --no-sandbox, and that belongs in the workflow that knows it is
# one rather than being switched on here by sniffing $CI -- a developer's Chrome
# should keep its sandbox.
CHROME_FLAGS = os.environ.get("CHROME_FLAGS", "").split()
ROOT = pathlib.Path(__file__).resolve().parent.parent
SIM = ROOT / "build" / "reader_sim"

# screen id -> (design board, human label). The id is what the simulator is
# expected to accept as its first argument; Phase 1 implements only "home".
V1_SCREENS = [
    ("home",         "Main.dc.html",       "Home"),
    ("library",      "Library.dc.html",    "Library"),
    ("reader",       "Reader.dc.html",     "Reader"),
    ("reader_menu",  "ReaderMenu.dc.html", "Reader menu"),
    # The three styled specimens. Their own rows rather than variants of `reader`,
    # because the mismatch percentage is per screen and folding them in would average
    # a styling regression away against a board that has no styles on it.
    # `reader_anchored` belongs here and NOWHERE ELSE -- it was also listed in
    # FLOW_SCREENS beside `peek`, which double-counted it on every run.
    ("reader_anchored",     "ReaderAnchored.dc.html",    "Reader - with a way back"),
    ("reader_chapter_open", "ReaderChapterOpen.dc.html", "Reader - chapter open"),
    ("reader_list",         "ReaderList.dc.html",        "Reader - list"),
    ("settings",     "Settings.dc.html",   "Settings"),
    ("sleep",        "Sleep.dc.html",      "Sleep"),
]

# PARKED, NOT DELETED. V1 is card-transfer only -- Wi-Fi was cut as too big --
# so these boards describe a V2 and comparing them would report a permanent
# "not implemented" for work nobody is doing. The files stay in design/ because
# they are real design work and V2 will want them; what changes is that the
# fidelity check stops counting them.
#
# Same treatment Instapaper already got (see the canvas page "V2 - Instapaper").
# If Wi-Fi returns, move these rows back into FLOW_SCREENS.
V2_SCREENS = [
    ("transfer",        "Transfer.dc.html",       "Send books (V2)"),
    ("wifi_picker",     "WifiPicker.dc.html",     "Join network (V2)"),
    ("wifi_password",   "WifiPassword.dc.html",   "Password entry (V2)"),
    ("wifi_error",      "WifiError.dc.html",      "Join failed (V2)"),
    ("wifi_connect",    "WifiConnect.dc.html",    "Wi-Fi connect (V2)"),
    ("wifi_settings",   "WifiSettings.dc.html",   "Wi-Fi settings (V2)"),
    ("setup_hotspot",   "SetupHotspot.dc.html",   "Setup hotspot (V2)"),
]

FLOW_SCREENS = [
    ("home_empty",      "HomeEmpty.dc.html",      "Home / empty"),
    ("home_unopened",   "HomeUnopened.dc.html",   "Home / nothing open"),
    ("home_charging",   "HomeCharging.dc.html",   "Home / charging"),
    ("home_missing",    "HomeMissing.dc.html",    "Home / missing book"),
    ("sleep_idle",      "SleepIdle.dc.html",      "Sleep / nothing open"),
    # The two cover modes. Their own rows rather than variants of `sleep`, because
    # the mismatch percentage is per screen and folding them in would average a
    # regression in one mode against a board that cannot show it. BOTH BOARDS DISPLAY
    # A COMMITTED PNG -- the firmware's own cover output -- so render_board has to
    # serve design/assets alongside index.html; see the note there.
    ("sleep_cover",         "SleepCover.dc.html",        "Sleep / cover"),
    ("sleep_cover_details", "SleepCoverDetails.dc.html", "Sleep / cover + details"),
    # THE WAKE OVER A COVER, and its own row for a reason the two above do not have:
    # it is the only sleep render that paints ONE pass. A wake gets one waveform, so
    # the firmware column here is Plane::Bw -- the Msb plane, the same picture at two
    # levels -- where its two siblings are the three-pass grayscale sequence. Folded
    # into either of them, a regression in the one-bit rendition would be averaged
    # against a board that cannot show it.
    ("sleep_cover_waking",  "SleepCoverWaking.dc.html",  "Sleep / cover, waking"),
    # THE LOADING STATE, and it is two boards rather than one because the mechanism
    # has two homes: it replaces the HINT BAR on a screen that draws one, and it
    # replaces the badge's words on Sleep, which draws no hint bar because it takes
    # no input. Same slot, same box, same tracked line -- so a change to one has to
    # be made to the other, which is exactly what a second board makes visible.
    ("library_opening", "LibraryOpening.dc.html", "Library / opening a book"),
    ("sleep_waking",    "SleepWaking.dc.html",    "Sleep / waking"),
    ("library_scrolled", "LibraryScrolled.dc.html", "Library / scrolled"),
    ("library_actions", "LibraryActions.dc.html", "Library actions"),
    ("delete_confirm",  "DeleteConfirm.dc.html",  "Delete confirm"),
    ("book_details",    "BookDetails.dc.html",    "Book details"),
    ("book_error",      "BookError.dc.html",      "Book error"),
    ("book_error_unreadable", "BookErrorUnreadable.dc.html", "Book error (unreadable)"),
    ("book_error_memory", "BookErrorMemory.dc.html", "Book error (out of memory)"),
    ("typography",      "Typography.dc.html",     "Typography"),
    ("contents",        "Contents.dc.html",       "Contents"),
    # Peek and return (3D). `peek` is the overlay -- book text over the veiled page
    # you are on. Its sibling `reader_anchored` -- the Reader with somewhere to go
    # back to, boarded separately so Reader.dc.html stays pinned as the no-anchor
    # common case -- is listed ONCE, up in V1_SCREENS with the other styled reader
    # specimens. It was in both lists for a while; see the duplicate-id guard in
    # main() for what that cost.
    # NAMES (3E). `names` is the alphabetical list -- two row heights, a rail -- and
    # `names_empty` is the same screen before reading has filled it, a variant rather
    # than a second screen. Selecting a row opens the peek, so there is no name
    # detail board to compare.
    ("names",           "Names.dc.html",          "Names"),
    ("names_empty",     "NamesEmpty.dc.html",     "Names / empty"),
    ("peek",            "Peek.dc.html",           "Peek"),
    ("bookmarks",       "Bookmarks.dc.html",      "Bookmarks"),
    ("book_end",        "BookEnd.dc.html",        "Book finished"),
    ("sd_missing",      "SdMissing.dc.html",      "No SD card"),
    ("low_battery",     "LowBattery.dc.html",     "Low battery"),
    ("battery_empty",   "BatteryEmpty.dc.html",   "Battery empty"),
    ("boot",            "Boot.dc.html",           "Boot"),
]


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):  # keep the tool's own output readable
        pass


def serve(directory):
    handler = lambda *a, **kw: QuietHandler(*a, directory=str(directory), **kw)
    httpd = socketserver.TCPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd.server_address[1]


def render_board(board_path, out_png, w, h):
    """Rasterise one .dc.html design board via Chrome, at frame size w x h.

    Every board's root element is authored inline as
    `width: {AUTHORED_W}px; height: {AUTHORED_H}px; ...`. To render at a
    different panel geometry without touching the board file, we inject a
    stylesheet rule (in the page the tool itself builds around the board's
    extracted markup) that selects on that literal inline-style substring and
    overrides width/height with !important -- which beats the board's own
    plain (non-!important) inline declaration per the CSS cascade. The same
    selector also matches the full-bleed overlay layers some boards use
    (a content layer and a "dim-veil" scrim, both authored at the identical
    frame size so they cover it edge-to-edge); overriding them together is
    correct, since they are meant to track the frame, not stay fixed at the
    authored size.
    """
    src = board_path.read_text()
    helmet = re.search(r"<helmet>(.*?)</helmet>", src, re.S)
    body = re.search(r"</helmet>\s*(.*?)</x-dc>", src, re.S)
    if not body:
        return None
    panel_body, panel_notes = recentre_panels(body.group(1), w)
    for note in panel_notes:
        print("    [board %s @ %dx%d] %s" % (board_path.stem, w, h, note))
    frame_override = (
        '<style>[style*="width: %dpx; height: %dpx;"]'
        '{width:%dpx !important;height:%dpx !important;}</style>'
        % (AUTHORED_W, AUTHORED_H, w, h))
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "index.html").write_text(
            '<!doctype html><meta charset="utf-8">'
            + (helmet.group(1) if helmet else "")
            + "<style>html,body{margin:0;background:#fff}</style>"
            + frame_override
            + panel_body)
        # THE BOARD'S ASSETS GO WITH IT. This directory is the document root, so a
        # board that says `src="assets/sleep-cover-480x800.png"` -- as both cover
        # sleep boards do -- 404s without this and renders a blank frame with a
        # broken-image glyph. It fails LOUDLY, since a blank panel against a
        # full-bleed cover is an enormous mismatch rather than a quiet pass, but it
        # fails, and the fix belongs here rather than in the board: inlining the PNG
        # as a data URI would make the markup a SECOND COPY of a file whose entire
        # purpose is that the board and the firmware read the same bytes.
        #
        # A symlink, not a copy: the pair is 263 KB and this runs once per board per
        # geometry (60 times for the full sheet), so copying would be ~16 MB of
        # temporary files for bytes nothing writes to. copytree is the fallback for a
        # filesystem that will not link.
        #
        # AND ITS ABSENCE IS A HARD ERROR rather than a skip, for the same reason a
        # board named in the list and absent from disk is: a tool that quietly serves
        # less than it claims still prints `ok`, and a board whose picture failed to
        # load would be measured against a blank design panel with nothing saying so.
        assets = ROOT / "design" / "assets"
        if not assets.is_dir():
            raise SystemExit(
                "design/assets is missing, so any board that displays a committed "
                "picture would render blank -- restore %s" % assets)
        try:
            os.symlink(assets, tmp / "assets", target_is_directory=True)
        except OSError:
            shutil.copytree(assets, tmp / "assets")
        port = serve(tmp)
        subprocess.run(
            [CHROME, "--headless", "--disable-gpu", "--force-device-scale-factor=1",
             *CHROME_FLAGS,
             "--hide-scrollbars", "--default-background-color=FFFFFFFF",
             # Without a virtual-time budget the screenshot can fire before the
             # Google Fonts webfont arrives, silently rendering the board in a
             # fallback face -- which made X4 and X3 panels disagree on type.
             "--virtual-time-budget=8000",
             f"--window-size={w},{h}", f"--screenshot={out_png}",
             f"http://127.0.0.1:{port}/index.html"],
            check=True, capture_output=True)
    p = pathlib.Path(out_png)
    return p if p.exists() else None


def recentre_panels(body, w):
    """Re-centre horizontally-centred absolute panels for a different canvas width.

    The frame override below rewrites anything authored at the full frame size, so
    the root, the content layer and the dim-veil scrim all track the new geometry.
    An overlay PANEL does not: it is authored as `left: 70px; width: 340px`
    (LibraryActions) or `left: 50px; width: 380px` (DeleteConfirm), neither of
    which mentions the frame size, so it kept its authored left edge and sat 24px
    off-centre in every X3 render. The firmware derives that edge from the canvas
    width and centres correctly, so the DESIGN column was the wrong one -- which is
    the worst way for this tool to be wrong, because a human reading the sheet
    trusts the design side.

    Done here rather than as a CSS rule because centring must be EARNED, not
    assumed. A blanket `left:0;right:0;margin:auto` would also silently centre a
    panel a board had deliberately placed off-centre, and the sheet would look
    right while hiding a real difference. So: only rewrite `left` when the authored
    numbers are symmetric about the authored frame, and say what was rewritten.

    Vertical needs nothing -- the boards centre with `top: 50%` and a
    `translateY(-50%)`, which is already geometry-independent (spec 4.1c).
    """
    if w == AUTHORED_W:
        return body, []
    notes = []

    def fix(m):
        style = m.group(0)
        if not re.search(r"position:\s*absolute", style):
            return style
        left = re.search(r"left:\s*(\d+)px", style)
        width = re.search(r"width:\s*(\d+)px", style)
        if not left or not width:
            return style
        lv, wv = int(left.group(1)), int(width.group(1))
        # Frame-sized layers belong to the !important override, not here.
        if wv == AUTHORED_W:
            return style
        if lv * 2 + wv != AUTHORED_W:
            notes.append("left %dpx width %dpx is NOT centred at %d; left alone"
                         % (lv, wv, AUTHORED_W))
            return style
        new_left = (w - wv) // 2
        notes.append("panel width %dpx: left %dpx -> %dpx" % (wv, lv, new_left))
        return style.replace("left: %dpx" % lv, "left: %dpx" % new_left, 1)

    return re.sub(r'style="[^"]*"', fix, body), notes


def render_sim(screen_id, out_png, w, h):
    """Ask the simulator for a screen at panel size w x h.

    Returns (path_or_None, status). The status separates the two ways a screen
    produces no firmware render, which look IDENTICAL on the sheet and are
    opposites: "unimplemented" is a screen the simulator has never heard of -- a
    board waiting for its screen, the normal state of a good third of this list
    -- and "failed" is a screen it DOES know and could not draw, which is a
    regression. While this returned a bare None the two were indistinguishable,
    which is why a crashed subcommand printed "not implemented" and exited 0.
    """
    if not SIM.exists():
        return None, "nosim"
    r = subprocess.run(
        [str(SIM), screen_id, out_png, "--canvas", f"{w}x{h}"],
        capture_output=True)
    p = pathlib.Path(out_png)
    if r.returncode == 0 and p.exists():
        return p, "ok"
    # The simulator names an id it does not recognise on stderr. Anything else
    # is a screen it accepted and then failed on -- a refused factory, a face
    # that would not load, a crash.
    if b"unknown screen" in r.stderr:
        return None, "unimplemented"
    return None, "failed"


def normalise(im, w, h):
    im = im.convert("L")
    if im.size != (w, h):
        # Nearest-neighbour keeps a firmware render's pixel grid honest.
        im = im.resize((w, h), Image.NEAREST)
    return im


def placeholder(text, w, h):
    im = Image.new("L", (w, h), 245)
    d = ImageDraw.Draw(im)
    for y in range(0, h, 8):        # faint diagonal hatch
        d.line([(0, y), (w, y - w)], fill=225)
    d.rectangle([0, 0, w - 1, h - 1], outline=170)
    d.text((w // 2 - 52, h // 2 - 4), text, fill=110)
    return im


def compose(rows, out, geom_keys, pairs_per_row=2):
    """rows: list of (label, geom_entries, impl) where geom_entries is a list
    of (key, device, w, h, design_img, firmware_img), one per geom_keys entry.
    """
    cap_h, sub_cap_h, gap_x, gap_y, pad, mid, geom_gap = 22, 16, 34, 22, 18, 10, 26

    def block_w(w):
        return w * 2 + mid

    row_block_w = (sum(block_w(GEOMETRIES[k]["w"]) for k in geom_keys)
                   + geom_gap * (len(geom_keys) - 1))
    row_block_h = max(sub_cap_h + GEOMETRIES[k]["h"] for k in geom_keys)

    cols = max(1, min(pairs_per_row, len(rows)))
    n_rows = (len(rows) + cols - 1) // cols
    w = pad * 2 + row_block_w * cols + gap_x * (cols - 1)
    h = pad * 2 + 30 + n_rows * (cap_h + row_block_h + gap_y)
    sheet = Image.new("L", (w, h), 232)
    d = ImageDraw.Draw(sheet)
    d.text((pad, pad), "ENCRE - DESIGN vs FIRMWARE   (each pair: design left, firmware right; "
                       "exact panel size, 1:1, no scaling)", fill=0)

    done = sum(1 for _, _, impl in rows if impl)
    d.text((pad, pad + 13), f"{done} of {len(rows)} screens implemented", fill=70)

    for i, (label, geom_entries, impl) in enumerate(rows):
        cx0 = pad + (i % cols) * (row_block_w + gap_x)
        cy = pad + 30 + (i // cols) * (cap_h + row_block_h + gap_y)
        mark = "" if impl else "   [NOT IMPLEMENTED]"
        d.text((cx0, cy + 4), f"{label}{mark}", fill=0 if impl else 110)

        gx = cx0
        for key, device, gw, gh, dimg, fimg in geom_entries:
            d.text((gx, cy + cap_h), f"{device} - {gw}x{gh}", fill=60)
            py = cy + cap_h + sub_cap_h
            for j, im in enumerate((dimg, fimg)):
                bx = gx + j * (gw + mid)
                sheet.paste(im, (bx, py))
                d.rectangle([bx - 1, py - 1, bx + gw, py + gh], outline=120)
            gx += block_w(gw) + geom_gap
    sheet.save(out)
    return sheet.size, done, len(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true",
                    help="accepted and ignored; every screen is the default now. "
                         "Kept so existing invocations and docs do not break.")
    # BOTH SPELLINGS, and the comma one is primary because it is the one that
    # was always documented (this module's docstring, and CLAUDE.md's rule on UI
    # work) and the only one that survives `make compare COMPARE_ARGS=...`,
    # where $(COMPARE_ARGS) is expanded UNQUOTED -- so a spelling needing shell
    # quoting inside a make variable would be a worse tool than this one.
    # `action="append"` is layered under it because argparse's default for a
    # plain option is to OVERWRITE: `--only home --only library` kept only
    # `library`, dropped `home` without a word, and printed a confident
    # "1/1 screens implemented" -- the exact reports-on-less-than-it-claims
    # shape as the card probe answered from cache and the default that compared
    # seven boards of thirty-odd. Accumulating cannot be wrong here: there is no
    # reading of a second --only under which the first was meant to be discarded.
    ap.add_argument("--only", action="append", metavar="IDS",
                    help="screen ids to compare, as a comma-separated list and/or "
                         "a repeated flag: `--only home,reader` and "
                         "`--only home --only reader` are the same run. Errors on "
                         "an id that matches nothing.")
    ap.add_argument("--geometry", choices=["x4", "x3", "both"], default="both",
                     help="device panel geometry to render: x4 (480x800), "
                          "x3 (528x792), or both (default)")
    ap.add_argument("--out", default=str(ROOT / "build" / "design-vs-firmware.png"))
    ap.add_argument("--pairs-per-row", type=int, default=2)
    ap.add_argument("--require-implemented", action="store_true",
                    help="exit non-zero if a screen the simulator KNOWS fails to "
                         "render. A board with no screen behind it is still fine -- "
                         "that is a third of this list. Off by default so a human "
                         "comparing mid-implementation is unaffected; CI passes it, "
                         "because otherwise a crashed subcommand reads as "
                         "'not implemented' and the run exits 0.")
    ap.add_argument("--export", metavar="DIR",
                    help="also write every render as a bare panel-size PNG into DIR, "
                         "named <screen>_<x4|x3>_<design|firmware>.png. No labels, "
                         "borders or padding -- suitable for overlaying in a design tool. "
                         "Unimplemented screens get no firmware file rather than a "
                         "placeholder, which would be useless to overlay.")
    args = ap.parse_args()

    # EVERY board, by default. This used to be `V1_SCREENS + (FLOW_SCREENS if
    # args.all else [])`, which meant the documented `make compare` compared Home
    # and Library and nothing else -- because four of the six screens the firmware
    # actually implements (ItemActions, DeleteConfirm, BookDetails, SdMissing) live
    # in FLOW_SCREENS. The fidelity check that CLAUDE.md calls "what keeps them
    # honest" was running on a third of what it claimed, and `--only sd_missing`
    # silently matched NOTHING because the filter ran over the already-narrowed
    # list. Same quiet-degradation shape as the card probe that read from cache and
    # kept reporting success (CLAUDE.md, Storage) -- a check that reports on less
    # than it says is worse than no check, because it is trusted.
    screens = V1_SCREENS + FLOW_SCREENS
    # A PARKED board is reachable by name but never by default -- see V2_SCREENS.
    # `--only transfer` still renders it, so a V2 design can be looked at without
    # putting it back in the count that measures V1.
    if args.only:
        screens = screens + V2_SCREENS
        # Every --only, then every comma inside each -- so the two spellings mix
        # and neither position nor spelling decides which ids a run honours.
        want = {piece.strip() for group in args.only
                for piece in group.split(",") if piece.strip()}
        if not want:
            # `--only ""` or `--only ,` would otherwise select nothing, find
            # nothing missing, and exit 0 reporting "0/0 screens implemented" --
            # the same quiet pass the guard below closes, reached by an empty
            # argument instead of an unknown one.
            raise SystemExit("--only was given but names no screen ids")
        screens = [s for s in screens if s[0] in want]
        missing = want - {s[0] for s in screens}
        if missing:
            # A typo used to render zero screens and report "0/0 implemented",
            # which reads like a pass. EVERY element is checked, so an id's
            # position in the list cannot decide whether a typo is caught: with
            # the old overwriting --only, a wrong id in any but the last flag
            # was discarded before it could be checked and the run exited 0.
            raise SystemExit(f"--only names no such screen: {', '.join(sorted(missing))}")

    # THE TABLES ARE A SET, NOT A BAG, and this is checked over all three of them
    # on every run rather than over the selection -- a duplicate is an authoring
    # mistake in the table, so it should not need the right --only to surface.
    #
    # `reader_anchored` was listed in V1_SCREENS and in FLOW_SCREENS at once (its
    # board was added FLOW-side design-first, then the implementation commit added
    # a second row beside the other styled specimens without noticing). Every
    # default `make compare` therefore rendered that board four times instead of
    # twice, counted it as two screens, and printed a denominator of 37 for the 36
    # screens that exist -- so both halves of the ratio were inflated, and a sheet
    # showed the same screen twice under two different labels. That is the mirror
    # of the absent board that shrank the denominator: in both cases the count is
    # over something other than the set of screens it claims to measure.
    #
    # ERRORING RATHER THAN DE-DUPLICATING, deliberately. Quietly collapsing the
    # rows would leave the second one in the file to be read as intentional, and
    # the two rows carried DIFFERENT labels -- so there is a real question about
    # which was meant, and this tool must not answer it by guessing.
    all_rows = V1_SCREENS + FLOW_SCREENS + V2_SCREENS
    seen = {}
    for sid, board, label in all_rows:
        seen.setdefault(sid, []).append((board, label))
    repeated = {sid: rows for sid, rows in seen.items() if len(rows) > 1}
    if repeated:
        raise SystemExit(
            "%d screen id(s) are listed more than once in compare-design.py, so "
            "the sheet would render and COUNT them twice -- keep one row:\n%s"
            % (len(repeated),
               "\n".join("  %s: %s" % (sid, ", ".join(f"design/{b} as {l!r}"
                                                      for b, l in rows))
                         for sid, rows in sorted(repeated.items()))))

    # A BOARD NAMED IN THIS LIST AND ABSENT FROM DISK IS A HARD ERROR, for exactly
    # the reason `--only` errors on an id it does not recognise. The render loop used
    # to print one line and `continue`, which dropped the screen from `rows`
    # ENTIRELY -- so the DENOMINATOR shrank and the sheet still reported a confident
    # "N/M screens implemented". Deleting a board therefore made the ratio look
    # BETTER while the check covered less, and `--only goto_page` against a deleted
    # board exited 0 reporting "0/0 screens implemented" -- the same quiet pass the
    # --only guard above was written to close, reached through a second door.
    # (That is how it shipped: GoToPage.dc.html was deleted with the reader menu's
    # `Go to page...` row on 2026-08-24 and this list kept naming it for four days.)
    # A check that reports on less than it claims is worse than no check, because it
    # is trusted. Checked up front rather than in the loop so it fails in a second
    # instead of after two minutes of rendering.
    absent = [(sid, board) for sid, board, _ in screens
              if not (ROOT / "design" / board).exists()]
    if absent:
        raise SystemExit(
            "design board missing for %d screen(s) -- restore the board, or remove "
            "the row from compare-design.py:\n%s"
            % (len(absent),
               "\n".join(f"  {sid}: design/{board}" for sid, board in absent)))
    if CHROME is None or not pathlib.Path(CHROME).exists():
        raise SystemExit(
            "Chrome not found%s. Set $CHROME to the binary, or install it at one of:\n%s"
            % ("" if CHROME is None else f" at {CHROME}",
               "\n".join(f"  {c}" for c in CHROME_CANDIDATES)))

    geom_keys = GEOMETRY_ORDER if args.geometry == "both" else [args.geometry]

    rows = []
    exported = []
    broken = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for sid, board, label in screens:
            bpath = ROOT / "design" / board

            geom_entries = []
            any_impl = False
            for key in geom_keys:
                g = GEOMETRIES[key]
                gw, gh, device = g["w"], g["h"], g["device"]
                bimg = render_board(bpath, str(tmp / f"{sid}_{key}_design.png"), gw, gh)
                simg, status = render_sim(sid, str(tmp / f"{sid}_{key}_sim.png"), gw, gh)
                impl = simg is not None
                if status == "failed":
                    broken.append(f"{sid} [{device} {gw}x{gh}]")
                any_impl = any_impl or impl
                dimg = (normalise(Image.open(bimg), gw, gh) if bimg
                        else placeholder("DESIGN FAILED", gw, gh))
                fimg = (normalise(Image.open(simg), gw, gh) if impl
                        else placeholder("NOT IMPLEMENTED", gw, gh))
                geom_entries.append((key, device, gw, gh, dimg, fimg))

                if args.export:
                    outdir = pathlib.Path(args.export)
                    outdir.mkdir(parents=True, exist_ok=True)
                    # Bare renders at exact panel size: what a design tool wants to
                    # stack. Skip a firmware file the screen does not have.
                    if bimg:
                        dimg.save(outdir / f"{sid}_{key}_design.png")
                        exported.append(f"{sid}_{key}_design.png")
                    if impl:
                        fimg.save(outdir / f"{sid}_{key}_firmware.png")
                        exported.append(f"{sid}_{key}_firmware.png")
                print(f"  {label:22s} [{device} {gw}x{gh}] design {'ok' if bimg else 'FAIL'}   "
                      f"firmware {'ok' if impl else 'not implemented'}")

            rows.append((label, geom_entries, any_impl))
        size, done, total = compose(rows, args.out, geom_keys, args.pairs_per_row)
    print(f"\nwrote {args.out} {size}  -  {done}/{total} screens implemented")
    if args.export:
        print(f"exported {len(exported)} bare panel PNGs to {args.export}/")
    if broken:
        # Printed whether or not the flag is set: a screen the simulator knows and
        # cannot draw is worth saying out loud even when nobody asked for a gate.
        print("\n%d screen render(s) FAILED (the simulator knows the id and could "
              "not draw it):\n%s" % (len(broken), "\n".join(f"  {b}" for b in broken)))
    if args.require_implemented:
        if not SIM.exists():
            raise SystemExit(
                f"--require-implemented, but there is no simulator at {SIM}. "
                "Every screen would report as unimplemented and the gate would "
                "pass on nothing having run. Build it first: make sim")
        if broken:
            raise SystemExit(1)


if __name__ == "__main__":
    main()

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
    python3 tools/compare-design.py --geometry x3      # X3 (528x792) only

The design board is served over a throwaway localhost server because Chrome
does not load file:// subresources reliably.
"""
import argparse
import http.server
import pathlib
import re
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

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
ROOT = pathlib.Path(__file__).resolve().parent.parent
SIM = ROOT / "build" / "reader_sim"

# screen id -> (design board, human label). The id is what the simulator is
# expected to accept as its first argument; Phase 1 implements only "home".
V1_SCREENS = [
    ("home",         "Main.dc.html",       "Home"),
    ("library",      "Library.dc.html",    "Library"),
    ("reader",       "Reader.dc.html",     "Reader"),
    ("reader_menu",  "ReaderMenu.dc.html", "Reader menu"),
    ("settings",     "Settings.dc.html",   "Settings"),
    ("transfer",     "Transfer.dc.html",   "Transfer"),
    ("sleep",        "Sleep.dc.html",      "Sleep"),
]

FLOW_SCREENS = [
    ("home_empty",      "HomeEmpty.dc.html",      "Home / empty"),
    ("home_missing",    "HomeMissing.dc.html",    "Home / missing book"),
    ("library_actions", "LibraryActions.dc.html", "Library actions"),
    ("delete_confirm",  "DeleteConfirm.dc.html",  "Delete confirm"),
    ("book_details",    "BookDetails.dc.html",    "Book details"),
    ("book_error",      "BookError.dc.html",      "Book error"),
    ("typography",      "Typography.dc.html",     "Typography"),
    ("contents",        "Contents.dc.html",       "Contents"),
    ("goto_page",       "GoToPage.dc.html",       "Go to page"),
    ("bookmarks",       "Bookmarks.dc.html",      "Bookmarks"),
    ("book_end",        "BookEnd.dc.html",        "Book finished"),
    ("wifi_picker",     "WifiPicker.dc.html",     "Join network"),
    ("wifi_password",   "WifiPassword.dc.html",   "Password entry"),
    ("wifi_error",      "WifiError.dc.html",      "Join failed"),
    ("wifi_connect",    "WifiConnect.dc.html",    "Wi-Fi connect"),
    ("wifi_settings",   "WifiSettings.dc.html",   "Wi-Fi settings"),
    ("setup_hotspot",   "SetupHotspot.dc.html",   "Setup hotspot"),
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
            + body.group(1))
        port = serve(tmp)
        subprocess.run(
            [CHROME, "--headless", "--disable-gpu", "--force-device-scale-factor=1",
             "--hide-scrollbars", "--default-background-color=FFFFFFFF",
             f"--window-size={w},{h}", f"--screenshot={out_png}",
             f"http://127.0.0.1:{port}/index.html"],
            check=True, capture_output=True)
    p = pathlib.Path(out_png)
    return p if p.exists() else None


def render_sim(screen_id, out_png, w, h):
    """Ask the simulator for a screen at panel size w x h.

    None when it does not implement the screen (at all, or at this geometry).
    """
    if not SIM.exists():
        return None
    r = subprocess.run(
        [str(SIM), screen_id, out_png, "--canvas", f"{w}x{h}"],
        capture_output=True)
    p = pathlib.Path(out_png)
    return p if r.returncode == 0 and p.exists() else None


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
    ap.add_argument("--all", action="store_true", help="include flows and states")
    ap.add_argument("--only", help="comma-separated screen ids")
    ap.add_argument("--geometry", choices=["x4", "x3", "both"], default="both",
                     help="device panel geometry to render: x4 (480x800), "
                          "x3 (528x792), or both (default)")
    ap.add_argument("--out", default=str(ROOT / "build" / "design-vs-firmware.png"))
    ap.add_argument("--pairs-per-row", type=int, default=2)
    args = ap.parse_args()

    screens = V1_SCREENS + (FLOW_SCREENS if args.all else [])
    if args.only:
        want = {s.strip() for s in args.only.split(",")}
        screens = [s for s in screens if s[0] in want]
    if not pathlib.Path(CHROME).exists():
        raise SystemExit(f"Chrome not found at {CHROME}")

    geom_keys = GEOMETRY_ORDER if args.geometry == "both" else [args.geometry]

    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for sid, board, label in screens:
            bpath = ROOT / "design" / board
            if not bpath.exists():
                print(f"  {label}: design board missing ({board}) - skipped")
                continue

            geom_entries = []
            any_impl = False
            for key in geom_keys:
                g = GEOMETRIES[key]
                gw, gh, device = g["w"], g["h"], g["device"]
                bimg = render_board(bpath, str(tmp / f"{sid}_{key}_design.png"), gw, gh)
                simg = render_sim(sid, str(tmp / f"{sid}_{key}_sim.png"), gw, gh)
                impl = simg is not None
                any_impl = any_impl or impl
                dimg = (normalise(Image.open(bimg), gw, gh) if bimg
                        else placeholder("DESIGN FAILED", gw, gh))
                fimg = (normalise(Image.open(simg), gw, gh) if impl
                        else placeholder("NOT IMPLEMENTED", gw, gh))
                geom_entries.append((key, device, gw, gh, dimg, fimg))
                print(f"  {label:22s} [{device} {gw}x{gh}] design {'ok' if bimg else 'FAIL'}   "
                      f"firmware {'ok' if impl else 'not implemented'}")

            rows.append((label, geom_entries, any_impl))
        size, done, total = compose(rows, args.out, geom_keys, args.pairs_per_row)
    print(f"\nwrote {args.out} {size}  -  {done}/{total} screens implemented")


if __name__ == "__main__":
    main()

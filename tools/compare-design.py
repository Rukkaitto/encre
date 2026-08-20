#!/usr/bin/env python3
"""Design-vs-firmware comparison sheet for every screen.

For each screen, rasterises its design board (a `.dc.html` from design/) in
headless Chrome at exact panel size and puts it beside the simulator's render
of the same screen, at 1:1 with no scaling. Screens the firmware does not draw
yet get a NOT IMPLEMENTED placeholder, so the sheet doubles as a progress
dashboard: run it and see how much of the design actually exists.

Not part of the build. Requires Google Chrome and Pillow.

    make test                       # build the simulator
    python3 tools/compare-design.py                  # V1 screens
    python3 tools/compare-design.py --all            # + flows and states
    python3 tools/compare-design.py --only home,reader

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

PANEL_W, PANEL_H = 480, 800
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


def render_board(board_path, out_png):
    """Rasterise one .dc.html design board at exact panel size via Chrome."""
    src = board_path.read_text()
    helmet = re.search(r"<helmet>(.*?)</helmet>", src, re.S)
    body = re.search(r"</helmet>\s*(.*?)</x-dc>", src, re.S)
    if not body:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "index.html").write_text(
            '<!doctype html><meta charset="utf-8">'
            + (helmet.group(1) if helmet else "")
            + "<style>html,body{margin:0;background:#fff}</style>"
            + body.group(1))
        port = serve(tmp)
        subprocess.run(
            [CHROME, "--headless", "--disable-gpu", "--force-device-scale-factor=1",
             "--hide-scrollbars", "--default-background-color=FFFFFFFF",
             f"--window-size={PANEL_W},{PANEL_H}", f"--screenshot={out_png}",
             f"http://127.0.0.1:{port}/index.html"],
            check=True, capture_output=True)
    p = pathlib.Path(out_png)
    return p if p.exists() else None


def render_sim(screen_id, out_png):
    """Ask the simulator for a screen. None when it does not implement it."""
    if not SIM.exists():
        return None
    r = subprocess.run([str(SIM), screen_id, out_png], capture_output=True)
    p = pathlib.Path(out_png)
    return p if r.returncode == 0 and p.exists() else None


def normalise(im):
    im = im.convert("L")
    if im.size != (PANEL_W, PANEL_H):
        # Nearest-neighbour keeps a firmware render's pixel grid honest.
        im = im.resize((PANEL_W, PANEL_H), Image.NEAREST)
    return im


def placeholder(text):
    im = Image.new("L", (PANEL_W, PANEL_H), 245)
    d = ImageDraw.Draw(im)
    for y in range(0, PANEL_H, 8):        # faint diagonal hatch
        d.line([(0, y), (PANEL_W, y - PANEL_W)], fill=225)
    d.rectangle([0, 0, PANEL_W - 1, PANEL_H - 1], outline=170)
    d.text((PANEL_W // 2 - 52, PANEL_H // 2 - 4), text, fill=110)
    return im


def compose(rows, out, pairs_per_row=2):
    cap_h, gap_x, gap_y, pad, mid = 22, 34, 18, 18, 10
    pair_w = PANEL_W * 2 + mid
    cols = min(pairs_per_row, len(rows))
    n_rows = (len(rows) + cols - 1) // cols
    w = pad * 2 + pair_w * cols + gap_x * (cols - 1)
    h = pad * 2 + 30 + n_rows * (cap_h + PANEL_H + gap_y)
    sheet = Image.new("L", (w, h), 232)
    d = ImageDraw.Draw(sheet)
    d.text((pad, pad), "ENCRE - DESIGN vs FIRMWARE   (each pair: design left, firmware right; "
                       "exact panel size 480x800, 1:1, no scaling)", fill=0)

    done = sum(1 for _, _, impl in rows if impl)
    d.text((pad, pad + 13), f"{done} of {len(rows)} screens implemented", fill=70)

    for i, (label, pair, impl) in enumerate(rows):
        cx = pad + (i % cols) * (pair_w + gap_x)
        cy = pad + 30 + (i // cols) * (cap_h + PANEL_H + gap_y)
        mark = "" if impl else "   [NOT IMPLEMENTED]"
        d.text((cx, cy + 4), f"{label}{mark}", fill=0 if impl else 110)
        for j, im in enumerate(pair):
            bx = cx + j * (PANEL_W + mid)
            sheet.paste(im, (bx, cy + cap_h))
            d.rectangle([bx - 1, cy + cap_h - 1, bx + PANEL_W, cy + cap_h + PANEL_H],
                        outline=120)
    sheet.save(out)
    return sheet.size, done, len(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true", help="include flows and states")
    ap.add_argument("--only", help="comma-separated screen ids")
    ap.add_argument("--out", default=str(ROOT / "build" / "design-vs-firmware.png"))
    ap.add_argument("--pairs-per-row", type=int, default=2)
    args = ap.parse_args()

    screens = V1_SCREENS + (FLOW_SCREENS if args.all else [])
    if args.only:
        want = {s.strip() for s in args.only.split(",")}
        screens = [s for s in screens if s[0] in want]
    if not pathlib.Path(CHROME).exists():
        raise SystemExit(f"Chrome not found at {CHROME}")

    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for sid, board, label in screens:
            bpath = ROOT / "design" / board
            if not bpath.exists():
                print(f"  {label}: design board missing ({board}) - skipped")
                continue
            bimg = render_board(bpath, str(tmp / f"{sid}_design.png"))
            simg = render_sim(sid, str(tmp / f"{sid}_sim.png"))
            rows.append((
                label,
                (normalise(Image.open(bimg)) if bimg else placeholder("DESIGN FAILED"),
                 normalise(Image.open(simg)) if simg else placeholder("NOT IMPLEMENTED")),
                simg is not None))
            print(f"  {label:22s} design {'ok' if bimg else 'FAIL'}   "
                  f"firmware {'ok' if simg else 'not implemented'}")
        size, done, total = compose(rows, args.out, args.pairs_per_row)
    print(f"\nwrote {args.out} {size}  -  {done}/{total} screens implemented")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Summarise the device's `[i]` interaction lines.

    make firmware && pio device monitor -e xteink | tee run.log
    python3 tools/latency.py run.log

Reads the per-interaction records the shell emits (shell/src/main.cpp, the
Interaction struct) and reports where the time between a button going down and
the panel being finished actually went.

WHAT IT DELIBERATELY REPORTS AND WHY:

  * MEDIAN, not mean. One interaction that hit a card doing internal housekeeping
    drags a mean somewhere no press ever was. The max is printed beside it so an
    outlier is visible rather than averaged away.
  * `net`, not `total`, in the headline. `ser` is the device talking to the USB
    host, which it only does because you are watching -- so `total` describes the
    device-plus-cable and `net` describes the device. If `ser` is a large fraction
    of `total`, the log is measuring itself and that is the first thing to fix.
  * GROUPED BY WHERE THE PRESS LANDED, because "a page turn" and "opening the
    Library" are different questions and their answers differ by seconds.
  * The stage columns SUM to net, so a row that does not add up is a bug in the
    instrumentation rather than a discovery.
"""
import re
import sys
from collections import defaultdict

# [i] #12 CONFIRM SHORT from=home to=library ev=1 | wait=7 pre=0 disp=1103 post=2
#     render=41 up=24 wave=712 | total=1889ms ser=31 net=1858
LINE = re.compile(
    r"\[i\] #(?P<n>\d+) (?P<button>\w+) (?P<kind>\w+) from=(?P<frm>\S+) to=(?P<to>\S+) "
    r"ev=(?P<ev>\d+) \| wait=(?P<wait>\d+) pre=(?P<pre>\d+) disp=(?P<disp>\d+) "
    r"post=(?P<post>\d+) render=(?P<render>\d+) up=(?P<up>\d+) wave=(?P<wave>\d+) \| "
    r"total=(?P<total>\d+)ms ser=(?P<ser>\d+) net=(?P<net>\d+)(?P<nopaint> paint=none)?"
)
FS = re.compile(r"\[fs\] (?P<op>\w+) (?P<path>\S+).*? in (?P<ms>\d+)ms")
# [render] total=266000us fill=102000/17 glyph=88000/28 veil=8000/1 icon=3000/8 other=65000
RENDER = re.compile(r"\[render\] total=(?P<total>\d+)us (?P<rest>.*)")
RENDER_SLOT = re.compile(r"(\w+)=(\d+)(?:/(\d+))?")
STAGES = ("wait", "pre", "disp", "post", "render", "up", "wave")


def median(xs):
    s = sorted(xs)
    m = len(s) // 2
    return s[m] if len(s) % 2 else (s[m - 1] + s[m]) / 2


def main(paths):
    rows = []
    fs = defaultdict(list)
    # A [render] line precedes the [i] line of the same paint, so the pending one
    # is attached to the next interaction rather than matched by an id.
    renders = []
    pending_render = None
    for path in paths or ["-"]:
        f = sys.stdin if path == "-" else open(path, errors="replace")
        for line in f:
            m = LINE.search(line)
            if m:
                d = m.groupdict()
                for k in STAGES + ("total", "ser", "net", "ev"):
                    d[k] = int(d[k])
                if pending_render is not None:
                    d["render_slots"] = pending_render
                    renders.append((d["to"], pending_render))
                    pending_render = None
                rows.append(d)
                continue
            m = RENDER.search(line)
            if m:
                slots = {k: int(v) for k, v, _ in RENDER_SLOT.findall(m.group("rest"))}
                slots["total"] = int(m.group("total"))
                pending_render = slots
                continue
            m = FS.search(line)
            if m:
                fs[(m.group("op"), m.group("path"))].append(int(m.group("ms")))

    if not rows:
        print("no [i] lines found -- is this a log from a build with the "
              "interaction instrumentation in it?")
        return 1

    ser = sum(r["ser"] for r in rows)
    tot = sum(r["total"] for r in rows)
    print(f"{len(rows)} interactions.  "
          f"serial overhead {ser}ms of {tot}ms total ({100 * ser / tot:.0f}%) -- "
          f"{'the cable is a real part of these numbers' if ser > tot * 0.05 else 'negligible'}")
    print()

    groups = defaultdict(list)
    for r in rows:
        groups[(r["button"], r["frm"], r["to"])].append(r)

    hdr = f"{'press':<34}{'n':>3}  {'net':>6}  {'max':>6}   " + "".join(
        f"{s:>7}" for s in STAGES)
    print(hdr)
    print("-" * len(hdr))
    for key in sorted(groups, key=lambda k: -median([r["net"] for r in groups[k]])):
        g = groups[key]
        button, frm, to = key
        name = f"{button} {frm}" + (f" -> {to}" if to != frm else "")
        line = f"{name:<34}{len(g):>3}  {median([r['net'] for r in g]):>6.0f}  "
        line += f"{max(r['net'] for r in g):>6}   "
        line += "".join(f"{median([r[s] for r in g]):>7.0f}" for s in STAGES)
        print(line)

    none = [r for r in rows if r["nopaint"]]
    if none:
        print(f"\n{len(none)} press(es) produced NO paint: " +
              ", ".join(sorted({f"{r['button']} on {r['frm']}" for r in none})))

    if renders:
        # PER SCREEN, because the render is the screen's; but the PRIMITIVES are
        # shared, so a slot that dominates here dominates everywhere it is drawn.
        by_screen = defaultdict(list)
        for screen, slots in renders:
            by_screen[screen].append(slots)
        keys = ["fill", "glyph", "veil", "dither", "icon", "other"]
        print("\nrender, microseconds (median), by primitive:")
        head = f"  {'screen':<16}{'n':>3}{'total':>9}" + "".join(f"{k:>9}" for k in keys)
        print(head)
        print("  " + "-" * (len(head) - 2))
        for screen in sorted(by_screen, key=lambda s: -median([r["total"] for r in by_screen[s]])):
            g = by_screen[screen]
            line = f"  {screen:<16}{len(g):>3}{median([r['total'] for r in g]):>9.0f}"
            for k in keys:
                vals = [r.get(k, 0) for r in g]
                line += f"{median(vals):>9.0f}" if any(vals) else f"{'-':>9}"
            print(line)

    if fs:
        print("\ncard operations over the slow threshold:")
        for (op, path), ms in sorted(fs.items(), key=lambda kv: -median(kv[1])):
            print(f"  {op:<9}{path:<34}{len(ms):>3}x  median {median(ms):.0f}ms  "
                  f"max {max(ms)}ms")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

# Contributing

Pull requests are welcome. Read this first: there are four rules a PR will
bounce on, and one ceiling worth knowing about before you spend an afternoon.

**`CLAUDE.md` is the real reference.** It is the project's working memory: what
was measured, what was tried and abandoned, and why. If you want to know how
something behaves or why it is the way it is, that is the document, and it is
more accurate than this one.

## Getting it building

```bash
git clone https://github.com/Rukkaitto/encre.git
cd encre
git submodule update --init
```

**The submodule step is not optional.** `freeink-sdk/` holds the display, input,
SD and battery drivers. Without it, `make firmware` fails with
`PackageException: not a directory`, which names neither the submodule nor the
fix. The desktop build doesn't need it, so a checkout can look perfectly healthy
and still not build firmware.

```bash
make test      # build core + run the unit and reference-render tests
make sim       # render Home to build/home.png
make compare   # every screen against its design drawing (~3 min)
```

`core/` is portable C++20 with no Arduino, ESP or host-OS dependency, so it
compiles for macOS and the ESP32 alike. The simulator renders any screen to a
PNG at exact panel size, which is where UI work actually happens:

```bash
./build/reader_sim <screen> out.png --canvas 528x792   # or 480x800 for the X4
./build/reader_sim <screen> out.png --bench 200        # what a render pass costs
```

Firmware is `make firmware`. PlatformIO installs outside `PATH`, so the Makefile
invokes it through `~/.platformio/penv/bin/python -m platformio`; override with
`make firmware PIO=/path/to/pio`. If it fails with
`Failed to install Python dependencies into penv`, that is transient. Retry it,
and don't run two builds at once.

## The four rules

**1. A UI change goes into the design HTML first, then the implementation.**
Never only in code, and not the other way round, including when the design
itself is what's wrong: fix the board in `design/`, then follow it. `make
compare` is what keeps the two honest, and changing only the implementation
silently invalidates it.

**2. Never re-bless a reference render to make a test pass.** A failing one
writes `build/<name>_candidate.png` precisely so the pixels can be looked at,
which is the only way to tell an intended change from a regression. If you
change one, say in the PR what moved and why.

**3. Re-run `cmake -S . -B build` after adding or removing a source file.**
CMake globs its sources, so a new file is silently ignored until you do.

**4. Branch names and commit subjects are enforced.** Install the hooks so you
find out before pushing rather than after:

```bash
make hooks         # commit-msg + pre-push, both bypassable with --no-verify
make conventions   # run the same check by hand
```

Subjects are [Conventional Commits](https://www.conventionalcommits.org/) with
the eleven standard types and a free-form scope: `feat(reader):`, `fix(peek):`,
`docs(readme):`. Branch names take git-flow's vocabulary plus `claude/`:
`feature/`, `bugfix/`, `chore/`, `docs/` and so on, then a lowercase slug.
**Your PR title matters too**: it becomes the commit subject when the PR is
squash-merged, so it has to pass the same check.

## The ceiling

Anything that touches the panel needs evidence from a real device, and only the
owner can produce it, because flashing is not something an automated agent here
is allowed to do. `shell/`, the layer that talks to the hardware, has no automated
tests at all, and several of this project's worst bugs have lived there: a
screen effect that passed every desktop test and smeared diagonally on glass, an
overlay painted onto white, a function that could only call itself.

So a change to rendering, the paint sequence, storage or power can get as far as
"passes everything on the desktop" and no further without someone holding an
X3. `docs/on-device-smoke-checklist.md` is what that verification looks like. It
isn't a reason not to send the PR. Just don't be surprised if it waits on
hardware.

## Finding your way around

| Path | What it is |
|---|---|
| `core/` | Portable C++20. Framebuffer, fonts, text, icons, layout, view models, themes. No Arduino, ESP or host-OS dependency. |
| `sim/` | Desktop simulator. Renders a screen to PNG at exact panel size. |
| `shell/` | The Arduino layer: device detection, display bring-up, the paint sequence. The only place that touches `freeink-sdk`. |
| `tools/` | Asset generators, the design comparison tool, the device log readers. |
| `design/` | `*.dc.html` design boards. **The source of truth for the UI.** |
| `docs/` | The spec, the roadmap, the smoke checklist, the release procedure. |
| `freeink-sdk/` | Submodule. MIT drivers for display, input, SD and battery. Never edited here. |

[Graft](https://github.com/trailhq/Graft) indexes the repo for coding agents.
The graph is a local cache like `build/`: gitignored and regenerable. What's
committed is the wiring in `.claude/`.

```bash
npm install -g @nanonets/graft
graft build
```

**One of its tools does not work here, and it's the one you'd want most.**
`graft callers <symbol>` returns nothing across files on this tree: a C++ free
function is declared in a header and defined in a `.cpp`, which makes the name
ambiguous, and graft drops an ambiguous cross-file edge rather than guessing.
Same-file edges are fine. Use `graft grep`, which is exhaustive and groups hits
by the enclosing symbol. `graft build --lsp` is the documented fix for exactly
this and doesn't help, because the ambiguity rule sits above the LSP layer.

It says so rather than reporting a confident zero, which is the only reason it's
worth having. The rest works well: `graft ask "<question>" --source` locates a
flow and inlines the code, `graft skeleton <file>` gives a file's API, `graft
map` orients.

## Where to read next

`CLAUDE.md`, for how the thing actually behaves and why. Then
`docs/superpowers/specs/2026-08-20-ereader-firmware-v1-design.md` for the spec
and `docs/superpowers/plans/2026-08-20-v1-roadmap.md` for the phases.

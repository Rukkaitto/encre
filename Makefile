.PHONY: test sim readme-images firmware fonts icons compare epubs epubs-bulk card-add card-remove zips conventions hooks canvas canvas-check canvas-test
# PlatformIO installs outside PATH by default; allow an override: make firmware PIO=/path/to/pio
#
# Invoked through its MODULE entry point rather than the `pio` launcher script,
# which is a convenience and NOT a way around the dependency check. That check
# can fail with "Failed to install Python dependencies into penv" while the
# toolchain itself is perfectly fine -- it did, mid-Phase-2C, with no change on
# our side -- and this comment used to claim the module entry point SKIPS it.
# It does not, and no entry point can: the check lives in the platform's builder
# script, ~/.platformio/platforms/espressif32/builder/penv_setup.py, not in the
# `pio` launcher. It is gated on having a network, runs `uv pip install
# --upgrade` when there is one, and exits 1 on failure -- so the failure means a
# PyPI hiccup or two builds sharing the `uv` cache. RETRY IT, and do not run two
# builds at once. See CLAUDE.md, "Hardware facts".
PIO_PY ?= $(HOME)/.platformio/penv/bin/python
PIO ?= $(if $(wildcard $(PIO_PY)),$(PIO_PY) -m platformio,pio)
PYTHON ?= python3

test:
	cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
# Branch name and commit subjects, the same check CI runs on a PR. Runnable here
# because a convention enforced only by CI is one you are told about after
# pushing, which is the worst moment to be asked to rewrite a commit message.
# Defaults to the current branch and origin/main..HEAD.
#
# The checker's own tests are `$(PYTHON) tools/test_check_conventions.py`, and
# they are NOT a target for tools/test_compare_design.py's reason: `make test`
# builds on a bare checkout with no Python. They drive the real .githooks/pre-push
# against throwaway repositories, so run them when the hook or the checker changes.
conventions:
	$(PYTHON) tools/check_conventions.py
# Install the commit-msg and pre-push hooks. One `git config` -- the hooks
# themselves are tracked in .githooks/, so they are reviewed like any other code
# and cannot drift per clone. The config lives in the common .git/config, so this
# covers every worktree at once. Both hooks run the same tools/check_conventions.py
# that CI runs, and both are bypassable with --no-verify by design: this is a fast
# local mirror of the gate, not a second source of truth.
hooks:
	git config core.hooksPath .githooks
	@echo "hooks installed: commit-msg, pre-push  (undo: git config --unset core.hooksPath)"
sim:
	cmake -S . -B build && cmake --build build -j --target reader_sim && ./build/reader_sim home build/home.png
# The four renders README.md shows, regenerated from the simulator so they are
# exactly what the panel draws rather than screenshots of something adjacent.
# Run it after any UI change that reaches one of these four screens; the release
# gate in docs/releasing.md is where it is remembered.
#
# 528x792 is the X3, which is the development device -- one geometry, because
# these illustrate the firmware rather than document both panels, and the X4's
# 480x800 would only differ in how much of a list fits.
#
# sleep_cover_details, which is the DEFAULT mode and so what a reader actually
# sees: the cover with the reading card over it.
#
# IT WAS sleep_cover FOR EXACTLY AS LONG AS #120 WAS OPEN. The details board
# named MIDDLEMARCH over a cover of ROMOLA -- a BOARD defect the render was
# faithful to -- and the cover-alone board was coherent by construction, because
# it draws no card and so has no second claim to contradict the picture. #120
# regenerated the assets from Standard Ebooks' Middlemarch, so all three of the
# constraints that collided now hold at once and this line moved back, which the
# comment it replaces said it would.
#
# WHAT COMES BACK WITH THE CARD is the badge sitting across the cover's own title
# band, cutting GEORGE ELIOT in half lengthwise. That is a SEPARATE open on-glass
# question, it is boarded, and its answer is NOT to move the badge -- the badge's
# position is Sleep.dc.html's. It is also what the device does in this mode, so a
# README showing this mode should show it.
README_SCREENS := reader home library sleep_cover_details
readme-images:
	cmake -S . -B build && cmake --build build -j --target reader_sim
	@mkdir -p docs/images
	@for s in $(README_SCREENS); do \
		./build/reader_sim $$s docs/images/$$s.png --canvas 528x792; \
	done
firmware:
	$(PIO) run -e xteink
# Rebuilds every generated font asset from the TTFs in assets/fonts. Needs
# freetype-py: pip install -r tools/requirements.txt
#
# Both TTFs are variable fonts, so every axis is pinned explicitly - relying on
# a face's variable default gave the chrome Space Grotesk Light (300) and 1px
# hairline stems. Literata's axes are its own defaults, spelled out on purpose;
# body text is revisited in Phase 3.
#
# The chrome ramp is a fixed set of roles (see the Phase 2A plan and
# core/include/reader/fontset.h): bitmaps are pre-rendered, so each distinct
# size AND weight is its own asset.
#
# Chrome is sized in POINTS at 150 DPI, which is CrossPoint's unit and the ramp
# design/Main.dc.html declares. ppem = pt * 150 / 72, so the six sizes land on
# 21/23/25/29/42/67 device pixels - roughly double the earlier px ramp, which was
# authored on a monitor and measured illegible on the ~220 PPI panel. Assets are
# named by pt so the unit is unambiguous in the filename; Literata stays on
# --size (pixels) until body text is revisited in Phase 3.
#
# A role names a size AND a weight (core/include/reader/fontset.h), and each
# asset below is one role. The boards set some sizes at more than one weight, so
# a size can be two assets -- 29px is, because --t-body is 24 runs at 500 and 11
# at the CSS default 400, and Home's author line is one of the 400s. It used to
# be drawn in the 500 face and measured 19% over the board's ink; a single face
# per size cannot do better than that, whichever weight it picks.
#
# Counted across all 46 boards (a run with no font-weight is CSS default 400):
#   --t-meta     21px   188 at 400,  17 at 500,   4 at 700   -> shipping 400+500+700
#   --t-label    23px    49 at 500,   8 at 400,   2 at 700   -> shipping 500
#   --t-value    25px   122 at 700,  61 at 500,   8 at 400   -> shipping 700
#   --t-body     29px    24 at 500,  11 at 400,   4 at 700   -> shipping 400+500+700
#   --t-title    42px     6 at 700                           -> shipping 700
#   --t-display  67px     2 at 700                           -> shipping 700
# The second weights not listed as shipping belong to screens that do not exist
# yet, and an unused face is 15-20KB of flash (10pt measures 27KB, so read that
# range as a floor). Add one when its screen lands: a line here, an entry in
# Role, a line in each loader.
#
# Meta700 is the first one added that way. Reader's footer sets its reading
# percentage at 21px/700 against the page counter's 400, and the weight IS the
# distinction -- it is what makes the percentage the primary reading of progress
# rather than one of two equal numbers. Bookmarks wants it twice and LowBattery
# once, so three screens were waiting on it. 27KB against a 6.5MB app partition
# running at ~11% is not a reason to redraw a board. FontSet::load checks the
# asset's declared ppem and weight against the role's, so a wrong binding is a
# load failure at boot rather than a screen that is quietly the wrong weight.
#
# The filenames and the embedded symbols carry the weight for the same reason.
fonts:
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 10 --weight 400 --autohint --bpp 2 --out assets/built/spacegrotesk_400_10pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 10 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_10pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 10 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_10pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 11 --weight 400 --autohint --bpp 2 --out assets/built/spacegrotesk_400_11pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 11 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_11pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 12 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_12pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 12 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_12pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 14 --weight 400 --autohint --bpp 2 --out assets/built/spacegrotesk_400_14pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 14 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_14pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 14 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_14pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 20 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_20pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 32 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_32pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/Literata.ttf --size 18 --weight 400 --opsz 12 --out assets/built/literata_18.rfnt
# Body text is NOT a bitmap set. Book CSS asks for an unbounded set of sizes
# (measured across the fixtures: 16, 32, 41, 51, 64 px, differing per book), so
# it is rasterised at runtime from a TTF in flash -- see
# core/include/reader/scalablefont.h. What ships is therefore a font FILE, and
# ttfprep.py resolves its variation axes and strips the tables stb_truetype
# cannot read: 955,132 bytes of variable Literata become 169,144 of static
# Literata with every one of its 1789 glyphs intact. The 785,988 bytes that go
# are variation deltas and layout tables the device could never have turned into
# a pixel -- including 104 KB of GPOS. GPOS still goes, but its KERNING no
# longer goes with it: ttfprep.py resolves the Extension lookups stb cannot
# follow and re-emits the pairs as a legacy `kern` table, 6064 pairs over
# fontc.py's subset for 36,402 bytes. Read ttfprep.py's docstring before
# assuming either half of that was a mistake. Axes are pinned explicitly here
# for the same reason fontc.py's are, and to the same values literata_18.rfnt
# uses.
	$(PYTHON) tools/ttfprep.py assets/fonts/Literata.ttf --axis wght=400 --axis opsz=12 --out assets/built/literata_body.ttf
# THE ITALIC IS A SECOND FILE, and it has to be: Literata.ttf carries `opsz` and
# `wght` and NO `ital` or `slnt` axis (macStyle 0x00, italicAngle 0), so no
# instancing of the roman produces a slanted glyph -- and Literata's italic is a
# true italic with its own letterforms at only -2 degrees, so an oblique
# transform was never a substitute either. Same axes, same unitsPerEm of 1000,
# pinned to the same coordinates as the roman so a line that mixes the two does
# not find their advances disagreeing.
	$(PYTHON) tools/ttfprep.py assets/fonts/LiterataItalic.ttf --axis wght=400 --axis opsz=12 --out assets/built/literata_italic.ttf
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_400_10pt.rfnt --out shell/src/font_meta400.h --symbol kFontMeta400
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_10pt.rfnt --out shell/src/font_meta500.h --symbol kFontMeta500
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_10pt.rfnt --out shell/src/font_meta700.h --symbol kFontMeta700
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_400_11pt.rfnt --out shell/src/font_label400.h --symbol kFontLabel400
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_11pt.rfnt --out shell/src/font_label500.h --symbol kFontLabel500
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_12pt.rfnt --out shell/src/font_value500.h --symbol kFontValue500
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_12pt.rfnt --out shell/src/font_value700.h --symbol kFontValue700
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_400_14pt.rfnt --out shell/src/font_body400.h --symbol kFontBody400
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_14pt.rfnt --out shell/src/font_body500.h --symbol kFontBody500
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_14pt.rfnt --out shell/src/font_body700.h --symbol kFontBody700
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_20pt.rfnt --out shell/src/font_title700.h --symbol kFontTitle700
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_32pt.rfnt --out shell/src/font_display700.h --symbol kFontDisplay700
	$(PYTHON) tools/embed_font.py assets/built/literata_body.ttf --out shell/src/font_body_serif.h --symbol kFontBodySerif
	$(PYTHON) tools/embed_font.py assets/built/literata_italic.ttf --out shell/src/font_body_serif_italic.h --symbol kFontBodySerifItalic
# Rebuilds the UI icon bitmaps from the design boards' own inline SVG, the same
# relationship `fonts` gives type. Needs Google Chrome (the only SVG rasteriser
# on this machine, and the one tools/compare-design.py measures the design with)
# and Pillow. The generated header is committed, so this only needs running when
# an icon's SVG or target size changes in tools/iconc.py.
#
# --sheet is optional and writes to build/, which is not committed: it is the
# 1:1-plus-6x contact sheet for eyeballing the set before re-blessing goldens.
# Reviewing that sheet is not optional -- the hand-drawn bitmaps this replaces
# passed review twice while the book icon read as the letters "OC".
icons:
	$(PYTHON) tools/iconc.py --out core/src/icons_data.h --sheet build/icons_sheet.png
# Design-vs-firmware contact sheet for EVERY screen (needs Chrome + Pillow).
# All 28 boards, both geometries, ~2.5 minutes. It used to default to the seven
# V1 screens, which meant it compared Home and Library and skipped four of the
# six screens the firmware implements -- see the comment in compare-design.py.
# COMPARE_ARGS=--geometry x3 narrows to one device panel (x4 480x800, x3
# 528x792); default renders and pairs both. --only screen_id,... filters screens
# and now errors on an id that matches nothing.
# Test EPUBs for the device and, later, for Phase 3's parser. A generator rather
# than checked-in binaries for the same reason fonts and icons are generated: a
# binary fixture is opaque, so when the parser disagrees with it you cannot see
# which of the two is wrong. Writes to build/epubs by default; EPUB_OUT=DIR to
# put them somewhere you can copy to a card.
EPUB_OUT ?= build/epubs
epubs:
	$(PYTHON) tools/mkepub.py --out $(EPUB_OUT)
# N small books with VARIED name lengths, for measuring what a book costs in RAM
# on the device -- the [library] boot line's per-row figure is dominated by the
# name string, and a three-book sample says nothing about the distribution.
# Copy the result into /books on the card and read the boot log.
BULK_N ?= 200
# Zip fixtures for the reader's archive layer, as a committed C++ header -- the
# same relationship iconc.py has with icons_data.h. Half of them are deliberately
# malformed, so no tool could have produced them; mkzip.py states what each is.
zips:
	$(PYTHON) tools/mkzip.py --out test/unit/zip_fixtures.h

# The named-entity table for the XML tokenizer, generated from Python's own HTML 4
# list -- the same relationship iconc.py has with icons_data.h. Regenerate and commit;
# nothing in the build runs this.
entities:
	$(PYTHON) tools/entities.py --out core/src/entity_table.h

epubs-bulk:
	$(PYTHON) tools/mkepub.py --out $(EPUB_OUT) --bulk $(BULK_N)
# ...and PUT THEM ON THE CARD, which generating them does not do. The card lives
# in the device, so it has to come out and go into a reader first; the script
# finds it by the firmware's own .reader/settings.json rather than by volume name,
# and refuses rather than guessing if that matches zero or several volumes.
# card-add is additive; card-remove takes off exactly the names in EPUB_OUT, so
# real books on the card are never touched.
card-add:
	sh tools/cardsync.sh add $(EPUB_OUT)
card-remove:
	sh tools/cardsync.sh remove $(EPUB_OUT)
compare: sim
	$(PYTHON) tools/compare-design.py $(COMPARE_ARGS)

# THE DESIGN CANVAS IS A GENERATED FILE, and these three targets are the only
# supported way to touch it. design/ereader-v1-ui.html is the published review
# surface (one artifact URL, see the design-change skill): the compiled canvas
# editor from tools/design-canvas/payload.template.html, with every
# design/*.dc.html and design/canvas.json seeded into its appifact-doc block.
#
# `make canvas` after ANY board change. Its predecessor lived untracked beside
# the skill that documented it and was lost, so for weeks the only way to change
# the canvas was to hand-edit 3 MB of generated JSON -- issue #60. It went six
# boards stale that way and nothing said so.
#
# NONE of this is wired into `make test`, for tools/test_compare_design.py's
# reason: the fast loop builds on a bare checkout with no interpreter, and node
# there would be worse than Python. `make compare COMPARE_ARGS=--require-canvas-current`
# is what makes staleness loud, and CI passes that flag.
CANVAS_SEED = node tools/design-canvas/seed-canvas.mjs \
	--template tools/design-canvas/payload.template.html \
	--out design/ereader-v1-ui.html --title "Encre UI" \
	--canvas design/canvas.json --boards-dir design

canvas:
	$(CANVAS_SEED)

# Verify the committed canvas is byte-identical to what the boards say it should
# be, and name what drifted if not. No Chrome, no simulator, about a second.
canvas-check:
	$(CANVAS_SEED) --check

# The generator's own test, including the round trip that proves it reproduces
# the committed canvas byte for byte from the canvas's own embedded content --
# the only specification the lost original left behind.
canvas-test:
	node tools/design-canvas/test_seed_canvas.mjs

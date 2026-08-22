.PHONY: test sim firmware fonts icons compare epubs
# PlatformIO installs outside PATH by default; allow an override: make firmware PIO=/path/to/pio
#
# Invoked through its MODULE entry point rather than the `pio` launcher script.
# The launcher runs a dependency check before it does anything, and that check can
# fail with "Failed to install Python dependencies into penv" while the toolchain
# itself is perfectly fine -- it did, mid-Phase-2C, with no change on our side.
# `python -m platformio` skips the check and builds identically, so a broken
# launcher no longer blocks a build or a flash.
PIO_PY ?= $(HOME)/.platformio/penv/bin/python
PIO ?= $(if $(wildcard $(PIO_PY)),$(PIO_PY) -m platformio,pio)
PYTHON ?= python3

test:
	cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
sim:
	cmake -S . -B build && cmake --build build -j --target reader_sim && ./build/reader_sim home build/home.png
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
#   --t-meta     21px   188 at 400,  17 at 500,   4 at 700   -> shipping 400
#   --t-label    23px    49 at 500,   8 at 400,   2 at 700   -> shipping 500
#   --t-value    25px   122 at 700,  61 at 500,   8 at 400   -> shipping 700
#   --t-body     29px    24 at 500,  11 at 400,   4 at 700   -> shipping 400+500+700
#   --t-title    42px     6 at 700                           -> shipping 700
#   --t-display  67px     2 at 700                           -> shipping 700
# The second weights not listed as shipping belong to screens that do not exist
# yet, and an unused face is 15-20KB of flash. Add one when its screen lands: a
# line here, an entry in Role, a line in each loader. FontSet::load checks the
# asset's declared ppem and weight against the role's, so a wrong binding is a
# load failure at boot rather than a screen that is quietly the wrong weight.
#
# The filenames and the embedded symbols carry the weight for the same reason.
fonts:
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 10 --weight 400 --autohint --bpp 2 --out assets/built/spacegrotesk_400_10pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 10 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_10pt.rfnt
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
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_400_10pt.rfnt --out shell/src/font_meta400.h --symbol kFontMeta400
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_10pt.rfnt --out shell/src/font_meta500.h --symbol kFontMeta500
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
# Design-vs-firmware contact sheet for every screen (needs Chrome + Pillow).
# COMPARE_ARGS=--all includes the flows and states.
# COMPARE_ARGS=--geometry x3 narrows to one device panel (x4 480x800, x3
# 528x792); default renders and pairs both. --only screen_id,... filters
# screens.
# Test EPUBs for the device and, later, for Phase 3's parser. A generator rather
# than checked-in binaries for the same reason fonts and icons are generated: a
# binary fixture is opaque, so when the parser disagrees with it you cannot see
# which of the two is wrong. Writes to build/epubs by default; EPUB_OUT=DIR to
# put them somewhere you can copy to a card.
EPUB_OUT ?= build/epubs
epubs:
	$(PYTHON) tools/mkepub.py --out $(EPUB_OUT)
compare: sim
	$(PYTHON) tools/compare-design.py $(COMPARE_ARGS)

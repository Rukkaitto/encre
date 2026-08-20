.PHONY: test sim firmware fonts compare
# PlatformIO installs outside PATH by default; allow an override: make firmware PIO=/path/to/pio
PIO ?= $(shell command -v pio 2>/dev/null || echo $(HOME)/.platformio/penv/bin/pio)
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
# The chrome ramp is a fixed set of six roles (see the Phase 2A plan): bitmaps
# are pre-rendered, so each distinct size is its own asset.
#
# Chrome is sized in POINTS at 150 DPI, which is CrossPoint's unit and the ramp
# design/Main.dc.html declares. ppem = pt * 150 / 72, so the six roles land on
# 21/23/25/29/42/67 device pixels - roughly double the earlier px ramp, which was
# authored on a monitor and measured illegible on the ~220 PPI panel. Assets are
# named by pt so the unit is unambiguous in the filename; Literata stays on
# --size (pixels) until body text is revisited in Phase 3.
fonts:
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 10 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_10pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 11 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_11pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 12 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_12pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 14 --weight 500 --autohint --bpp 2 --out assets/built/spacegrotesk_500_14pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 20 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_20pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --pt 32 --weight 700 --autohint --bpp 2 --out assets/built/spacegrotesk_700_32pt.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/Literata.ttf --size 18 --weight 400 --opsz 12 --out assets/built/literata_18.rfnt
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_10pt.rfnt --out shell/src/font_meta.h --symbol kFontMeta
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_11pt.rfnt --out shell/src/font_label.h --symbol kFontLabel
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_12pt.rfnt --out shell/src/font_value.h --symbol kFontValue
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_14pt.rfnt --out shell/src/font_body.h --symbol kFontBody
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_20pt.rfnt --out shell/src/font_title.h --symbol kFontTitle
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_32pt.rfnt --out shell/src/font_display.h --symbol kFontDisplay
# Design-vs-firmware contact sheet for every screen (needs Chrome + Pillow).
# COMPARE_ARGS=--all includes the flows and states.
# COMPARE_ARGS=--geometry x3 narrows to one device panel (x4 480x800, x3
# 528x792); default renders and pairs both. --only screen_id,... filters
# screens.
compare: sim
	$(PYTHON) tools/compare-design.py $(COMPARE_ARGS)

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
# are pre-rendered, so each distinct pixel size is its own asset.
fonts:
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 12 --weight 500 --autohint --out assets/built/spacegrotesk_500_12.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 13 --weight 500 --autohint --out assets/built/spacegrotesk_500_13.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 14 --weight 700 --autohint --out assets/built/spacegrotesk_700_14.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 17 --weight 500 --autohint --out assets/built/spacegrotesk_500_17.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 24 --weight 700 --autohint --out assets/built/spacegrotesk_700_24.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 44 --weight 700 --autohint --out assets/built/spacegrotesk_700_44.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/Literata.ttf --size 18 --weight 400 --opsz 12 --out assets/built/literata_18.rfnt
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_12.rfnt --out shell/src/font_meta.h --symbol kFontMeta
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_13.rfnt --out shell/src/font_label.h --symbol kFontLabel
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_14.rfnt --out shell/src/font_value.h --symbol kFontValue
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_17.rfnt --out shell/src/font_body.h --symbol kFontBody
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_24.rfnt --out shell/src/font_title.h --symbol kFontTitle
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_44.rfnt --out shell/src/font_display.h --symbol kFontDisplay
# Design-vs-firmware contact sheet for every screen (needs Chrome + Pillow).
# COMPARE_ARGS=--all includes the flows and states.
# COMPARE_ARGS=--geometry x3 narrows to one device panel (x4 480x800, x3
# 528x792); default renders and pairs both. --only screen_id,... filters
# screens.
compare: sim
	$(PYTHON) tools/compare-design.py $(COMPARE_ARGS)

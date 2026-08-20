.PHONY: test sim firmware fonts
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
# hairline stems. Chrome gets two weights: 500 for letterspaced labels, 700 for
# values. Literata's axes are its own defaults, spelled out on purpose; body
# text is revisited in Phase 3.
fonts:
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 16 --weight 500 --autohint --out assets/built/spacegrotesk_500_16.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 16 --weight 700 --autohint --out assets/built/spacegrotesk_700_16.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/Literata.ttf --size 18 --weight 400 --opsz 12 --out assets/built/literata_18.rfnt
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_500_16.rfnt --out shell/src/font_spacegrotesk_500_16.h --symbol kUiLabelFont
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_700_16.rfnt --out shell/src/font_spacegrotesk_700_16.h --symbol kUiValueFont

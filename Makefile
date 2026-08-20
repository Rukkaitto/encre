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
fonts:
	$(PYTHON) tools/fontc.py assets/fonts/SpaceGrotesk.ttf --size 16 --out assets/built/spacegrotesk_16.rfnt
	$(PYTHON) tools/fontc.py assets/fonts/Literata.ttf --size 18 --out assets/built/literata_18.rfnt
	$(PYTHON) tools/embed_font.py assets/built/spacegrotesk_16.rfnt --out shell/src/font_spacegrotesk_16.h --symbol kUiFont

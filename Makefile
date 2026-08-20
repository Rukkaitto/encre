.PHONY: test sim firmware
# PlatformIO installs outside PATH by default; allow an override: make firmware PIO=/path/to/pio
PIO ?= $(shell command -v pio 2>/dev/null || echo $(HOME)/.platformio/penv/bin/pio)

test:
	cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
sim:
	cmake -S . -B build && cmake --build build -j --target reader_sim && ./build/reader_sim home build/home.png
firmware:
	$(PIO) run -e xteink

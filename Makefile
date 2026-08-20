.DEFAULT_GOAL := simulator

# CrossInk Gujarati + FreshRSS developer workflow.
#
# This Makefile is intentionally rooted in this repository.  The optional
# CROSSPOINT_WRAPPER path is used only for public font catalogs/build scripts
# and the redacted FreshRSS fixture; generated fonts, simulator files, build
# output, and credentials stay local to this checkout.

ROOT := $(CURDIR)
CROSSPOINT_WRAPPER ?= $(ROOT)/../crosspoint

VENV_PYTHON := $(ROOT)/.venv/bin/python
PYTHON ?= $(if $(wildcard $(VENV_PYTHON)),$(VENV_PYTHON),$(if $(wildcard $(CROSSPOINT_WRAPPER)/crosspoint-reader/.venv/bin/python),$(CROSSPOINT_WRAPPER)/crosspoint-reader/.venv/bin/python,python3))
PIO ?= $(if $(wildcard $(ROOT)/.venv/bin/pio),$(ROOT)/.venv/bin/pio,$(if $(wildcard $(CROSSPOINT_WRAPPER)/crosspoint-reader/.venv/bin/pio),$(CROSSPOINT_WRAPPER)/crosspoint-reader/.venv/bin/pio,pio))
CMAKE ?= cmake
CTEST ?= ctest
JOBS ?= 2

PLATFORMIO_CORE_DIR ?= $(ROOT)/.platformio
PIO_ENV ?= simulator-X3
DEVELOPMENT_FIRMWARE_ENVS ?= default sticky
HOST_TEST_BUILD ?= $(ROOT)/test/build
FS_DIR ?= $(ROOT)/fs_
# Simulator EPUBs are part of this repository. Keep EPUB_DIR overridable for
# developers who want to mount a larger private library.
EPUB_DIR ?= $(ROOT)/epubs

# The existing wrapper contains the public catalog/tooling.  Override these
# when the repository is moved away from the sibling wrapper checkout.
FONT_CATALOG ?= $(CROSSPOINT_WRAPPER)/gujarati-fonts.json
FONT_BUILDER ?= $(CROSSPOINT_WRAPPER)/tools/build-gujarati-fonts.py
RSS_FONT_BUILDER ?= $(CROSSPOINT_WRAPPER)/tools/build-rss-list-font.py
FONT_CACHE ?= $(ROOT)/.work/fonts
FONT_OUTPUT_DIR ?= $(ROOT)/lib/EpdFont/scripts/output
FONT_SIZES ?= 12,14,16,18
GUJARATI_FONTS ?= Rasa,HindVadodara,MuktaVaani
FALLBACK_FONT_DIR ?= $(CROSSPOINT_WRAPPER)/crosspoint-reader/lib/EpdFont/scripts/downloaded_fonts/NotoSerifGujarati
FALLBACK_REGULAR ?= $(FALLBACK_FONT_DIR)/NotoSerifGujarati-Regular.ttf
FALLBACK_BOLD ?= $(FALLBACK_FONT_DIR)/NotoSerifGujarati-Bold.ttf

FRESHRSS_TEMPLATE ?= $(CROSSPOINT_WRAPPER)/rss/freshrss.json
FRESHRSS_FILE ?= $(FS_DIR)/.crosspoint/freshrss.json
SIMULATOR_FS_SOURCE ?= $(CROSSPOINT_WRAPPER)/crosspoint-reader/fs_

FEATURE_TEST_TARGETS := \
	GujaratiShaperTest \
	RssPolicyTest \
	RssItemStateStoreTest \
	RssParserTest \
	RssItemCacheTest \
	FreshRssCacheTest \
	FreshRssJsonParserTest \
	FreshRssApiClientTest \
	ReaderLayoutTest

.PHONY: help setup build firmware simulator simulator-x3 simulator-headless \
	smoke configure-tests host-tests test test-gujarati test-rss \
	prepare-simulator-fs prepare-epub-fixture prepare-freshrss prepare-settings prepare-fonts generate-fonts \
	generate-rss-font firmware-development verify status clean clean-tests check-pio

help:
	@echo "CrossInk Gujarati + FreshRSS targets"
	@echo "  make setup                 Create .venv and install build dependencies"
	@echo "  make build                Build the configured PlatformIO environment"
	@echo "  make firmware             Build the X3/X4 device firmware"
	@echo "  make firmware-development Build development firmware (default + sticky)"
	@echo "  make simulator             Build and run the X3 simulator"
	@echo "  make simulator-headless   Run the simulator with SDL dummy video"
	@echo "  make smoke                Run the headless EPUB simulator smoke test"
	@echo "  make test                 Build and run Gujarati/RSS/layout host tests"
	@echo "  make test-gujarati        Run Gujarati shaping tests"
	@echo "  make test-rss             Run RSS/FreshRSS/cache tests"
	@echo "  make prepare-settings     Reset the simulator fixture to Rasa 12pt defaults"
	@echo "  make prepare-epub-fixture Link the Gujarati EPUB fixture into fs_/books"
	@echo "  make generate-fonts       Generate Rasa, HindVadodara, MuktaVaani"
	@echo "  make generate-rss-font    Generate IBM Plex Sans Condensed + Rasa"
	@echo "  make verify               Build, test, and check the worktree"
	@echo "  make status               Show this repository's Git status"
	@echo ""
	@echo "Overrides: PIO_ENV=simulator-X3, DEVELOPMENT_FIRMWARE_ENVS='default sticky x4-pro', JOBS=4, EPUB_DIR=/path/to/epubs, CROSSPOINT_WRAPPER=/path/to/crosspoint"

setup:
	@test -x "$(VENV_PYTHON)" || python3 -m venv "$(ROOT)/.venv"
	@"$(VENV_PYTHON)" -m pip install -r "$(ROOT)/requirements.txt" platformio

check-pio:
	@test -x "$(PIO)" || (echo "PlatformIO not found: $(PIO)"; echo "Run: make setup"; exit 1)

build: check-pio
	@cd "$(ROOT)" && PLATFORMIO_CORE_DIR="$(PLATFORMIO_CORE_DIR)" "$(PIO)" run -e "$(PIO_ENV)"

firmware: PIO_ENV := default
firmware: build

firmware-development: check-pio
	@set -eu; \
	for env_name in $(DEVELOPMENT_FIRMWARE_ENVS); do \
		echo "Building development firmware environment: $$env_name"; \
		cd "$(ROOT)" && PLATFORMIO_CORE_DIR="$(PLATFORMIO_CORE_DIR)" "$(PIO)" run -e "$$env_name"; \
	done

simulator: PIO_ENV := simulator-X3
simulator: prepare-simulator-fs prepare-epub-fixture prepare-freshrss prepare-settings build
	@cd "$(ROOT)" && PLATFORMIO_CORE_DIR="$(PLATFORMIO_CORE_DIR)" "$(PIO)" run -e "$(PIO_ENV)" -t run_simulator

simulator-x3: simulator

simulator-headless: PIO_ENV := simulator-X3
simulator-headless: prepare-simulator-fs prepare-epub-fixture prepare-freshrss prepare-settings build
	@cd "$(ROOT)" && SDL_VIDEODRIVER=dummy PLATFORMIO_CORE_DIR="$(PLATFORMIO_CORE_DIR)" "$(PIO)" run -e "$(PIO_ENV)" -t run_simulator

smoke: check-pio
	@cd "$(ROOT)" && PATH="$(dir $(PIO)):$${PATH}" "$(PYTHON)" scripts/run_simulator_smoke_test.py --env simulator

prepare-simulator-fs:
	@mkdir -p "$(FS_DIR)/.crosspoint" "$(FS_DIR)/.fonts"
	@if [ ! -f "$(FS_DIR)/.fonts/Rasa/Rasa_16.cpfont" ] && [ -d "$(SIMULATOR_FS_SOURCE)" ]; then \
		echo "Importing simulator fixture from $(SIMULATOR_FS_SOURCE)"; \
		cp -R "$(SIMULATOR_FS_SOURCE)/." "$(FS_DIR)/"; \
	fi
	@test -f "$(FS_DIR)/.fonts/Rasa/Rasa_16.cpfont" || echo "Warning: Rasa CPFonts are not installed; run 'make generate-fonts'."
	@test -f "$(FS_DIR)/.fonts/IBMPlexSansCondensed/IBMPlexSansCondensed_12.cpfont" || echo "Warning: RSS list CPFont is not installed; run 'make generate-rss-font'."

prepare-epub-fixture:
	@EPUB_DIR="$(EPUB_DIR)" BOOKS_DIR="$(FS_DIR)/books" "$(PYTHON)" -c 'import os; src=os.path.abspath(os.environ["EPUB_DIR"]); dst=os.path.abspath(os.environ["BOOKS_DIR"]); parent=os.path.dirname(dst); assert os.path.isdir(src), f"Gujarati EPUB directory not found: {src}"; os.makedirs(parent, exist_ok=True); assert not os.path.exists(dst) or os.path.islink(dst), f"Refusing to replace non-symlink simulator books path: {dst}"; os.path.lexists(dst) and os.unlink(dst); os.symlink(os.path.relpath(src, parent), dst)'
	@echo "Simulator EPUB fixture: $$(readlink "$(FS_DIR)/books")"

prepare-freshrss:
	@mkdir -p "$$(dirname "$(FRESHRSS_FILE)")"
	@if [ ! -f "$(FRESHRSS_FILE)" ]; then \
		if [ -f "$(FRESHRSS_TEMPLATE)" ]; then \
			cp "$(FRESHRSS_TEMPLATE)" "$(FRESHRSS_FILE)"; \
		else \
			echo "FreshRSS template not found; simulator will start without an account."; \
		fi; \
	fi
	@if [ -f "$(FRESHRSS_FILE)" ]; then \
		echo "FreshRSS configuration: $(FRESHRSS_FILE)"; \
		echo "Edit that ignored file before using Refresh; no credentials are stored in Git."; \
	else \
		echo "Configure FreshRSS in the simulator's Settings screen if needed."; \
	fi

prepare-settings:
	@mkdir -p "$(FS_DIR)/.crosspoint"
	@SETTINGS_FILE="$(FS_DIR)/.crosspoint/settings.json" "$(PYTHON)" -c 'import json, os; path=os.environ["SETTINGS_FILE"]; data=json.load(open(path, encoding="utf-8")) if os.path.exists(path) else {}; data.update({"fontSize": 12, "sdFontFamilyName": "Rasa", "rssFontSize": 12, "rssSdFontFamilyName": "Rasa", "rssListFontSize": 12, "rssListSdFontFamilyName": "IBMPlexSansCondensed"}); open(path, "w", encoding="utf-8").write(json.dumps(data, ensure_ascii=False, separators=(",", ":")))'
	@echo "Simulator defaults: Rasa 12pt reader/article font; IBM Plex Sans Condensed 12pt RSS list font."

prepare-fonts:
	@if [ ! -f "$(FS_DIR)/.fonts/Rasa/Rasa_16.cpfont" ] || [ ! -f "$(FS_DIR)/.fonts/HindVadodara/HindVadodara_16.cpfont" ] || [ ! -f "$(FS_DIR)/.fonts/MuktaVaani/MuktaVaani_16.cpfont" ]; then \
		$(MAKE) --no-print-directory generate-fonts; \
	fi
	@test -f "$(FS_DIR)/.fonts/Rasa/Rasa_16.cpfont"
	@test -f "$(FS_DIR)/.fonts/HindVadodara/HindVadodara_16.cpfont"
	@test -f "$(FS_DIR)/.fonts/MuktaVaani/MuktaVaani_16.cpfont"

generate-fonts:
	@test -f "$(FONT_CATALOG)" || (echo "Gujarati catalog not found: $(FONT_CATALOG)"; echo "Set FONT_CATALOG=/path/to/gujarati-fonts.json"; exit 1)
	@test -f "$(FONT_BUILDER)" || (echo "Gujarati font builder not found: $(FONT_BUILDER)"; echo "Set FONT_BUILDER=/path/to/build-gujarati-fonts.py"; exit 1)
	@test -f "$(FALLBACK_REGULAR)" || (echo "Noto Serif Gujarati fallback not found: $(FALLBACK_REGULAR)"; echo "Set FALLBACK_FONT_DIR=/path/to/NotoSerifGujarati"; exit 1)
	@test -f "$(FALLBACK_BOLD)" || (echo "Noto Serif Gujarati fallback not found: $(FALLBACK_BOLD)"; echo "Set FALLBACK_FONT_DIR=/path/to/NotoSerifGujarati"; exit 1)
	@mkdir -p "$(FONT_OUTPUT_DIR)" "$(FONT_CACHE)"
	@"$(PYTHON)" "$(FONT_BUILDER)" \
		--catalog "$(FONT_CATALOG)" \
		--fonts "$(GUJARATI_FONTS)" \
		--default-font Rasa \
		--source-tree "$(ROOT)" \
		--output-dir "$(FONT_OUTPUT_DIR)" \
		--cache-dir "$(FONT_CACHE)" \
		--fallback-regular "$(FALLBACK_REGULAR)" \
		--fallback-bold "$(FALLBACK_BOLD)" \
		--sizes "$(FONT_SIZES)"
	@mkdir -p "$(FS_DIR)/.fonts"
	@cp -R "$(FONT_OUTPUT_DIR)/." "$(FS_DIR)/.fonts/"
	@echo "Installed Gujarati CPFonts in $(FS_DIR)/.fonts"

generate-rss-font:
	@test -f "$(RSS_FONT_BUILDER)" || (echo "RSS font builder not found: $(RSS_FONT_BUILDER)"; echo "Set RSS_FONT_BUILDER=/path/to/build-rss-list-font.py"; exit 1)
	@mkdir -p "$(FONT_OUTPUT_DIR)" "$(FONT_CACHE)"
	@if [ ! -f "$(FONT_CACHE)/shaping/Rasa/Rasa-pua-mapping.json" ]; then \
		$(MAKE) --no-print-directory generate-fonts GUJARATI_FONTS=Rasa; \
	fi
	@mkdir -p "$(FONT_CACHE)/rss-builder-output"
	@"$(PYTHON)" "$(RSS_FONT_BUILDER)" \
		--source-tree "$(ROOT)" \
		--output-dir "$(FONT_CACHE)/rss-builder-output" \
		--cache-dir "$(FONT_CACHE)" \
		--sizes "$(FONT_SIZES)"
	@mkdir -p "$(FONT_OUTPUT_DIR)/IBMPlexSansCondensed"
	@"$(PYTHON)" "$(ROOT)/lib/EpdFont/scripts/fontconvert_sdcard.py" \
		--regular "$(FONT_CACHE)/downloads/IBMPlexSansCondensed/IBMPlexSansCondensed-Regular.ttf" \
		--bold "$(FONT_CACHE)/downloads/IBMPlexSansCondensed/IBMPlexSansCondensed-Bold.ttf" \
		--fallback-regular "$(FONT_CACHE)/instances/Rasa/regular.ttf" \
		--fallback-bold "$(FONT_CACHE)/instances/Rasa/bold.ttf" \
		--fallback-regular "$(FALLBACK_REGULAR)" \
		--fallback-bold "$(FALLBACK_BOLD)" \
		--intervals "latin-ext,gujarati,punctuation" \
		--sizes "$(FONT_SIZES)" \
		--name IBMPlexSansCondensed \
		--output-dir "$(FONT_OUTPUT_DIR)/IBMPlexSansCondensed" \
		--pua-mapping "$(FONT_CACHE)/shaping/Rasa/Rasa-pua-mapping.json"
	@mkdir -p "$(FS_DIR)/.fonts"
	@cp -R "$(FONT_OUTPUT_DIR)/IBMPlexSansCondensed" "$(FS_DIR)/.fonts/"
	@echo "Installed RSS list CPFonts in $(FS_DIR)/.fonts/IBMPlexSansCondensed"

configure-tests:
	@"$(CMAKE)" -S "$(ROOT)/test" -B "$(HOST_TEST_BUILD)" -DCMAKE_BUILD_TYPE=Release

host-tests: configure-tests
	@"$(CMAKE)" --build "$(HOST_TEST_BUILD)" --target $(FEATURE_TEST_TARGETS) --parallel "$(JOBS)"
	@"$(CTEST)" --test-dir "$(HOST_TEST_BUILD)" --output-on-failure -R 'Gujarati|Rss|Fresh|ReaderLayout'

test: host-tests

test-gujarati: configure-tests
	@"$(CMAKE)" --build "$(HOST_TEST_BUILD)" --target GujaratiShaperTest --parallel "$(JOBS)"
	@"$(CTEST)" --test-dir "$(HOST_TEST_BUILD)" --output-on-failure -R '^GujaratiShaper\\.'

test-rss: configure-tests
	@"$(CMAKE)" --build "$(HOST_TEST_BUILD)" --target RssPolicyTest RssItemStateStoreTest RssParserTest RssItemCacheTest FreshRssCacheTest FreshRssJsonParserTest FreshRssApiClientTest ReaderLayoutTest --parallel "$(JOBS)"
	@"$(CTEST)" --test-dir "$(HOST_TEST_BUILD)" --output-on-failure -R 'Rss|Fresh|ReaderLayout'

verify: build host-tests
	@git diff --check
	@if rg -n '^(<<<<<<<|=======|>>>>>>>)' --glob '!test/build/**' --glob '!.pio/**' --glob '!fs_/**' .; then \
		echo "Conflict markers found"; \
		exit 1; \
	fi
	@echo "CrossInk Gujarati + FreshRSS verification passed"

status:
	@git status --short --branch

clean: check-pio
	@"$(PIO)" run -e "$(PIO_ENV)" -t clean

clean-tests:
	@rm -rf "$(HOST_TEST_BUILD)"

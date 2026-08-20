# CrossInk

> Personal fork of [CrossInk](https://github.com/uxjulia/CrossInk) (a [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) fork) with improved typography, reading stats, **Gujarati text rendering**, and a **FreshRSS-backed RSS reader**.
>
> Upstream CrossInk focuses on fonts and lightweight reading statistics. This checkout adds two features on top: [Gujarati script rendering](./docs/gujarati-rendering.md) and an [RSS reader backed by FreshRSS](./docs/rss-reader.md). See [This build's additions](#this-builds-additions).

## Supported devices

| Device | MCU | PlatformIO env | Notes |
| --- | --- | --- | --- |
| Xteink X3 | ESP32-C3 | `default` | SPI SD, no PSRAM |
| Xteink X4 | ESP32-C3 | `default` | SPI SD, no PSRAM |
| Seeed reTerminal Sticky | ESP32-S3R8 | `sticky` | Touch, PSRAM framebuffer |
| Xteink X4 Pro | ESP32-S3R8 | `x4-pro-simulator` only | Hardware firmware not in this repo yet; SDK + simulator profile exist |

Firmware for X3/X4 and Sticky is built from this repository. Use the [device simulator](#development-device-simulator) for UI and reader work without flashing hardware.

---

## What's different in this fork

The upstream CrossInk goal is to keep CrossPoint stable while layering preferred typography and lightweight reading statistics. This checkout extends that with Gujarati shaping and a cache-first FreshRSS reader.

<table>
  <tr>
    <td align="center">
      <img src="./docs/images/bitter-small-15-margin.jpg" alt="Font: Bitter, Size: 12 pt, Margin: 15" /><br/>
      <em>Font: Bitter, Size: 12 pt, Margin: 15</em>
    </td>
    <td align="center">
      <img src="./docs/images/reading-stats.jpg" alt="Reading Stats with custom front button mapping shown" /><br/>
      <em>Reading Stats with custom front button mapping shown</em>
    </td>
  </tr>
</table>

### Highlights

- New reader fonts: Lexend Deca and Bitter.
- Unicode emoji and miscellaneous symbols support (a limited subset).
- Reader font sizes: 10 pt, 12 pt, 14 pt, and 16 pt.
- Added ~~strikethrough~~ support.
- Made <u>underlines</u> thicker for better visibility.
- Added a custom `Minimal` theme and sleep screen option for the minimalists out there.
- Added a custom `Dashboard` theme and sleep screen option for reading stats enthusiasts.
- Added support for `<hr>` section breaks.
- Added support for "redaction" style rendering.
- Added improved support for tables with simple markup.
- Added ability to add bookmarks.
- Added ability to remap front buttons that only applies in the reader.
- Added Bionic Reading and Guide Dots as optional reader modes.
- Added Force Paragraph Indents for books that render as one giant wall of text.
- Added ability to pin a sleep image as a favorite. The favorited image will always be displayed when your sleep settings are set to `Custom` or `Cover + Custom` (when no cover is available).
- Added more in-reader control remapping options for side buttons, short power button clicks, and long-press menu actions.
- Added ability to mark a book as finished from the in-book menu. A pop-up will also display once 99% of the book is reached. This status allows tracking of total books read.
- Added ability to move finished books to "Read" folder.
- In-book menu to quickly adjust reader options without having to exit the book.
- Reading stats: total books read, total reading time, number of sessions, pages turned, average session time, pages turned per minute. You can also set your reading stats as your sleep screen.
- All-time reading stats [syncing](./docs/reading-stats-sync.md) between two CrossInk devices.
- Reading [progress sync](./docs/nearby-position-sync.md) between two CrossInk devices.
- Added customizable Auto Page Turn Interval (anything between 5-120 seconds).
- Added ability to view Recent Books as a 3x3 grid view.
- For a version-by-version list, see upstream [CrossInk releases](https://github.com/uxjulia/CrossInk/releases).

---

## This build's additions

Everything in **Highlights** comes from CrossInk. The two features below exist only in this checkout.

### Gujarati rendering

Gujarati EPUBs render as properly shaped text instead of tofu boxes. Shaping runs at layout time (in `ParsedText::addWord`) and the result is cached in `section.bin`, so re-reads pay no shaping cost.

- Conjunct ligatures (`ક્ષ`, `સ્ત્ર`, …) via PUA glyphs embedded in the SD-card font.
- Pre-base `િ` matra reordered into visual order.
- Reph (`ર+્`), subjoined Ra (`ટ્ર`), anusvara (`ં`), and chandrabindu (`ઁ`) drawn as zero-advance overlays by `GfxRenderer`.
- Bundled **Rasa** family (the default Gujarati reader font) plus **Noto Serif Gujarati**, both built with matching PUA conjuncts.
- Home and Recent Books screens prewarm the SD fallback font, so book titles and authors in Gujarati (or CJK) render correctly instead of falling back to placeholders.

The shaper is self-contained in [`lib/GujaratiShaper/`](./lib/GujaratiShaper/); upstream touch points are a single line in `ParsedText` plus ~60 lines in `GfxRenderer`.

Install the font via Settings → System → Manage Fonts, then pick it under Settings → Reader → Font Family. Clear `.crosspoint/` on the SD card after upgrading, since the section cache format changed.

See [Gujarati rendering](./docs/gujarati-rendering.md) for the full architecture, porting notes, and font-generation scripts.

### RSS reader (FreshRSS)

Home → **RSS Feeds** opens a cache-first reader backed by a self-hosted [FreshRSS](https://freshrss.org/) instance via its read-only Google Reader API.

- Dashboard shortcuts: All Articles, Unread Articles, Starred Articles, Reading Queue, Categories, and Subscriptions, plus a **Refresh** row that triggers the only network operation.
- Refresh authenticates once, fetches subscriptions/tags/reading-list, and writes a snapshot to the SD card. Later refreshes send a modified-since cursor and merge only changed entries; malformed or inconsistent delta responses rebuild the full snapshot.
- Browsing is cache-only. WiFi connects only while a refresh is running and is disconnected in every activity's `onExit()`, the same pattern the OPDS browser uses. If a refresh fails, the existing cache is still readable.
- Articles open in a paginated view with real formatting: centered bold title, first-line paragraph indents, bold/italic and alignment preserved from the source HTML, justified body text, and the same half/fast e-ink refresh cycling as the book reader.
- A device-local **Reading Queue** (read-later, 256 items max) is stored in `/.crosspoint/rss/state.bin` and never synced back to FreshRSS.
- Reader → **RSS Settings** is separate from EPUB/TXT settings: list density, date display, unread filter, an RSS-only font profile (family, size, spacing, indent, alignment, margins), button/mark-read behavior, and cache stats with a clear action.
- Feed list text defaults to **IBM Plex Sans Condensed** at 12 pt, whose CPFont embeds Rasa glyphs (including shaped Gujarati PUA glyphs), so Gujarati feed titles render without a second font load.

Upstream [`SCOPE.md`](./SCOPE.md) lists RSS readers as out of scope for battery and memory reasons; this implementation stays inside those constraints but is a fork-only feature by design.

See [RSS reader](./docs/rss-reader.md) for setup, FreshRSS configuration, the cache format, and icon attribution.

---

### Reader fonts

The default fonts have been replaced with Lexend Deca and Bitter. These fonts have been chosen specifically to improve reading fluency and e-ink performance. These 'sturdier' typefaces feature uniform stroke weights and open geometries, allowing the X4/X3 to render crisp, high-contrast text with font-aliasing on while significantly reducing ghosting and artifacts.

- [Lexend Deca](https://fonts.google.com/specimen/Lexend+Deca) — A research-backed sans-serif typeface designed to improve reading fluency. Lexend was engineered based on the theory that reading issues are often a design problem (visual crowding) rather than a cognitive one.
- [Bitter](https://fonts.google.com/specimen/Bitter) — A "contemporary" slab serif typeface for text, it is specially designed for comfortably reading on digital screens. The consistent stroke weight of Bitter helps it render particularly well on e-ink devices. The medium weight has been chosen specifically for improved rendering on the X4/X3.

The UI now uses [Inter](https://fonts.google.com/specimen/Inter) as the display font which has improved readability at smaller sizes.

### Emojis and misc glyphs

Support for a limited set of Unicode [Emoticons](https://unicode-explorer.com/b/1F600) and [Miscellaneous Symbols](https://unicode-explorer.com/b/2600) using [Noto Emoji](https://fonts.google.com/noto/specimen/Noto+Emoji) and [Noto Sans Symbols](https://fonts.google.com/noto/specimen/Noto+Sans+Symbols) font.

---

### Font sizes

CrossInk includes 10 pt, 12 pt, 14 pt, and 16 pt built-in reader font sizes.

See [SD Card Fonts](./docs/sd-card-fonts.md) for installing additional font families and size ranges.

---

### Reader features

Reader Options, Bionic Reading, Guide Dots, Force Paragraph Indents, reading stats, and finished-book behavior are documented in [Reader Features](./docs/reader-features.md).

### Custom button actions

CrossInk adds configurable button shortcuts.

See [Controls](./docs/controls.md) for the full action list and defaults.

---

## Tips for the best reading experience

CrossInk runs on an ESP32-C3 with limited RAM, so very large folders or complex EPUBs can be slower than they would be on a phone, tablet, or desktop app.

- Keep folders under about 200 files. For the smoothest browsing, aim for 50-100 files per folder.
- Having 1000+ books on the SD card is fine if they are split into smaller folders, such as by author, series, genre, or read/unread status.
- Avoid putting every book in the SD card root. The file browser has to scan and sort the current folder before it can show it.
- Text-first EPUBs are the best fit. Large image-heavy EPUBs, scanned books, comics, and omnibus files with thousands of sections may load slowly or fail under memory pressure.
- As a rough target, EPUBs under 20 MB tend to work the best. Files over 50 MB may still work, but they are more likely to be slow or memory-sensitive, especially if they contain many large images.
- If an EPUB is unusually slow, try [optimizing](./docs/webserver.md#epub-optimization) it with the built-in web optimizer (via File Transfer) before copying it to the SD card: remove unused high-resolution images, split very large omnibus files, and avoid embedding multiple full font families when possible.
- Use a reliable SD card and leave some free space. CrossInk stores settings, reading progress, cache files, stats, and generated book data on the card.

## Development device simulator

The [device simulator](https://github.com/uxjulia/crossink-simulator) renders the e-ink display in an SDL2 window so firmware changes can be sanity-checked without flashing hardware.

See [Simulator](./docs/simulator.md) for setup, platform notes, keyboard controls, and cache tips.

---

## Installation

The fastest way to install CrossInk firmware is [Inky](https://inky.crossink.dev/#flash-tools), CrossInk's web companion app.

Download a `firmware-*.bin` from the [upstream CrossInk releases page](https://github.com/uxjulia/CrossInk/releases) (or build from this repository), then flash with the web installer or command line.

See [Installation](./docs/installation.md) for step-by-step flashing and revert instructions.

---

## Documentation

- [User Guide](./docs/user-guide.md)
- [Installation](./docs/installation.md)
- [SD Card Fonts](./docs/sd-card-fonts.md)
- [Gujarati rendering](./docs/gujarati-rendering.md)
- [RSS reader](./docs/rss-reader.md)
- [Reader Features](./docs/reader-features.md)
- [Dictionary](./docs/dictionary.md)
- [Controls](./docs/controls.md)
- [Simulator](./docs/simulator.md)
- [Data Cache](./docs/data-cache.md)
- [Web server usage](./docs/webserver.md)
- [Web server endpoints](./docs/webserver-endpoints.md)
- [Common issues](./docs/troubleshooting.md)
- [Project scope](./SCOPE.md)
- [Development docs](./docs/development/README.md)
- [Agent guide](./AGENTS.md) — canonical instructions for AI coding agents

---

## Development quick start

CrossInk uses [PlatformIO](https://platformio.org/) for building and flashing firmware. AI agents should follow [AGENTS.md](./AGENTS.md).

See [Getting Started](./docs/development/getting-started.md) for prerequisites, clone setup, and validation commands.

### Clone

```sh
git clone --recursive https://github.com/monk-blade/CrossInk.git
cd CrossInk
git submodule update --init --recursive   # if already cloned without --recursive
```

Track upstream CrossInk with:

```sh
git remote add upstream https://github.com/uxjulia/CrossInk.git
```

### Nix/NixOS

Nix/NixOS users can enter the development shell with either `nix develop` (flakes) or `nix-shell`:

```bash
nix develop -f nix
# or
nix-shell nix
```

To flash a connected ESP32-C3 device, enable PlatformIO's udev rules in your NixOS configuration:

```nix
services.udev.packages = with pkgs; [ platformio-core.udev ];
```

After rebuilding the system configuration, reconnect the device or reload udev rules.

### Makefile workflow (Gujarati + FreshRSS)

This checkout includes a root `Makefile` for day-to-day feature development:

```sh
make setup                  # create .venv and install Python deps
make simulator              # build and run the SDL simulator
make test                   # run Gujarati, RSS, and layout host tests
make firmware               # build X3/X4 firmware (pio run -e default)
make firmware-development   # build default + sticky
make verify                 # build, test, and check git status
```

Run `make help` for the full target list. Font generation may use an optional sibling wrapper checkout (`CROSSPOINT_WRAPPER`, default `../crosspoint`).

### Build / flash / monitor

Connect your Xteink X4 or X3 via USB-C and run:

```sh
pio run -e default --target upload
```

| Goal | Command |
| --- | --- |
| X3/X4 firmware | `pio run -e default` |
| Seeed Sticky firmware | `pio run -e sticky` |
| SDL simulator | `pio run -e simulator` |
| X3-sized simulator | `pio run -e simulator-X3` |
| Sticky simulator (touch) | `pio run -e sticky-simulator` |
| X4 Pro simulator | `pio run -e x4-pro-simulator` |

See [Testing and Debugging](./docs/development/testing-debugging.md) for serial logging, static analysis, and bug-report guidance.

---

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/` | App orchestration, settings/state, activity implementations (home, reader, settings, network, boot/sleep) |
| `src/activities/browser/` | OPDS and RSS feed/article list activities |
| `src/FreshRss*.cpp`, `src/Rss*.cpp` | FreshRSS client, cache, and sync (fork feature) |
| `lib/` | EPUB parsing/layout, fonts, i18n, filesystem helpers, HAL wrappers |
| `lib/GujaratiShaper/` | Self-contained Gujarati shaping module (fork feature) |
| `lib/RssParser/` | RSS/Atom HTML → rich text for the FreshRSS reader (fork feature) |
| `freeink-sdk/` | Hardware SDK submodule (display, input, storage, battery, network) — [docs](https://freeink.org/docs) |
| `web/` | Web portal sources; compiled by `scripts/build_web.py` into `src/network/html/*.generated.h` |
| `docs/` | User and developer documentation (also built by the `site/` Astro project) |
| `test/` | Unit tests and EPUB fixtures |
| `scripts/` | Build, codegen, and release tooling |
| `fs_/` | Sample SD card contents for the simulator |
| `Makefile` | Gujarati + FreshRSS developer targets (`make help`) |
| `AGENTS.md` | Canonical AI agent instructions |
| `SCOPE.md`, `GOVERNANCE.md`, `CHANGELOG.md` | Scope, community principles, release history |

## Internals

The ESP32-C3 has about 380 KB of usable RAM, so CrossInk stores reusable book and device data on the SD card instead of rebuilding everything in memory. ESP32-S3 targets (Sticky) place the display framebuffer in PSRAM to free internal DRAM for EPUB rendering.

See [Data Cache](./docs/data-cache.md) for the `.crosspoint` layout and [File Formats](./docs/file-formats.md) for binary cache details.

## Notice on contributions

This is a personal development fork. It tracks [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk) as upstream. Pull requests are not accepted here; feature ideas belong in upstream [CrossInk discussions](https://github.com/uxjulia/CrossInk/discussions) or, for core reader scope, [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader).

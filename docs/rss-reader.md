# RSS Feed Reader (CrossInk-only)

**This feature is not part of upstream CrossPoint Reader.** Upstream's
[`SCOPE.md`](../SCOPE.md) explicitly lists RSS readers as out of scope under
"Active Connectivity" — background Wi-Fi drains the battery and complicates
the single-core ESP32-C3. This implementation avoids exactly that failure
mode (WiFi is connected only while the RSS screens are open, and disconnected
in every activity's `onExit()`, identical to how the existing OPDS Book
Browser already behaves) but it is still a CrossInk fork feature, integrated
the same way as the Gujarati rendering work: as a patch
(`patches/rss-reader.patch` in the wrapper repo) applied to an otherwise
unmodified CrossPoint checkout, not something intended for an upstream PR.

## Feature overview

- **Home → RSS Feeds** — a proper Tabler RSS outline icon (see "Icons" below), not
  a repurposed WiFi glyph — opens a cache-only dashboard with **All Articles**,
  **Unread Articles**, **Starred Articles**, **Reading Queue**, **Categories**,
  and **Subscriptions** shortcuts.
- The first manual refresh authenticates once with FreshRSS, fetches only the
  read-only subscription, tag, and global reading-list API endpoints, and
  writes a full committed snapshot. Later refreshes send the committed
  modified-since cursor (`ot`) and merge only new/modified entries; unchanged
  records are copied directly from the previous SD snapshot. Unsupported,
  stale, malformed, or inconsistent delta responses automatically rebuild the
  full snapshot. No linked article page or individual feed URL is ever
  requested.
  Refresh is a dashboard row (Feeds > Refresh) that runs the sync described
  above; it starts the only network operation and is safe to use while
  browsing the cached copy. The header button and footer-hint refresh
  shortcuts, along with the settings row that configured them, were removed
  once the dashboard row shipped.
- The home browser is category-first. **Categories** opens all articles in a
  selected category directly. **Subscriptions** opens category → feed →
  article navigation. Both views filter the same global snapshot locally,
  without another network request; empty categories and empty subscription
  rows are omitted. The first-visit no-cache path opens a virtual All Articles
  list so the user can complete the initial refresh.
- Navigation rows use their category/feed icons without numeric prefixes.
  Article rows use a filled dot for unread and a hollow dot for read instead of
  a repeated page/list glyph and expose cached source,
  author, date, unread, local star, and queue state; FreshRSS browsing keeps only the
  compact key list and the current visible page in RAM, while all admitted
  records remain on the SD card.
- Categories and subscriptions with no cached articles are omitted. Category
  labels fall back to the name after `user/-/label/` when FreshRSS omits the
  display label, and home badges are computed from the committed index.
- If a feed has already been fetched at least once, its cached copy opens
  immediately with **no WiFi involved at all** — WiFi is opt-in via Refresh,
  not a precondition for rereading a feed you've already fetched. If a
  refresh fails, the reader falls back to the cache it already has rather
  than erroring.
- Selecting an item opens a paginated article view with real formatting: the
  title renders as a centered, bold heading at the top of the article with a
  clean divider before the body (not just truncated in the footer status bar);
  body paragraphs get a first-line
  indent, the same 3-space-width default the EPUB reader uses; bold/italic
  and paragraph alignment (center, or an explicit HTML right-align) survive
  from the source HTML; and body text justifies the same way EPUB body text
  does. A manual refresh performs a bounded FreshRSS API session
  (authentication plus read-only metadata/article pages), streams every
  admitted item into a new SD-card snapshot, and disconnects WiFi before the
  list is shown. It never fetches a linked article page or an individual feed
  URL. Opening an item is strictly cache-only; even a truncated
  article displays its cached 32 KB prefix and never refetches a linked page.
  Opening an item marks it read, and pages use the same half/fast e-ink refresh
  cycling as the book reader.
- Article rows use unread/read dot markers and show cached source/author
  metadata, date, local star state with a Tabler star outline, and a `Q` queue marker.

The reading queue is a device-local read-later list. Long-confirm toggles the
selected article in the queue; it is independent of read and star state, is
persisted in `/.crosspoint/rss/state.bin`, and is never sent to FreshRSS. The
queue is bounded to 256 article IDs. If FreshRSS no longer returns a queued
article, the queue view renders an unavailable placeholder; long-confirm
removes that local ID without a network fallback.

## RSS settings

Reader → RSS Settings is separate from EPUB/TXT settings. It provides:

- **View:** all articles or unread-only, compact/comfortable list density,
  human-readable/compact/hidden dates, and independent article-title and
  separator switches. The unread filter stores only item indexes while the
  list is open; it never copies article bodies.
- **Font:** an RSS-only built-in or SD-card font family and size, line and
  paragraph spacing, optional first-line indentation, alignment, and screen margins. Entering an article
  temporarily activates this profile and restores the normal reader profile
  on exit.

The packaged FreshRSS list profile defaults to [IBM Plex Sans Condensed](https://fonts.google.com/specimen/IBM+Plex+Sans+Condensed?preview.script=Latn)
at 12pt for compact Latin titles and metadata. Its generated SD CPFont embeds
Rasa glyphs, including shaped Gujarati PUA glyphs, so Gujarati list text uses
Rasa automatically without a second runtime font load. Article bodies retain
the separate Rasa profile at 16pt.
- **Controls:** left/right/disabled refresh action, mark-read timing (open or
  final page), final-page behavior, footer hint visibility, and the star
  shortcut. The selected row can be starred with the configured control, and
  the article view shows its current starred state.
- **Cache:** item/complete/truncated counts, snapshot/index storage usage,
  queue count, cache age when the board clock is valid, and a clear action.
  Clearing removes FreshRSS snapshots, temporary/legacy RSS files, read markers,
  and stars while leaving the account credentials intact. The old cache-policy JSON fields remain
  accepted for compatibility but are deprecated no-ops.

The default unread filter is **off**. With it enabled, an empty feed shows
“No unread articles.” New cache snapshots do not perform LRU eviction.

Cached articles are opened before any network step. During an explicit refresh,
the list shows a bounded progress bar for admitted articles while each record
is shaped and written to the temporary SD snapshot.

## Icons

FreshRSS uses the [Tabler Icons](https://github.com/tabler/tabler-icons) outline
family, rasterized from its official SVGs at 24px with a 2px stroke. Tabler is
MIT licensed. Dashboard icons communicate the destination; article rows use
cheap drawing primitives for state markers, avoiding a font lookup per row:

| Icon | Source | Used at | File |
|---|---|---|---|
| All articles | Tabler `list`, 24px | FreshRSS dashboard | `src/components/icons/tabler_rss24.h` |
| Unread articles | Tabler `mail`, 24px | FreshRSS dashboard | `src/components/icons/tabler_rss24.h` |
| Starred articles | Tabler `star`, 24px | Dashboard and article rows | `src/components/icons/tabler_rss24.h` |
| Reading queue | Tabler `bookmark`, 24px | FreshRSS dashboard | `src/components/icons/tabler_rss24.h` |
| Categories | Tabler `folder`, 24px | Dashboard/category rows | `src/components/icons/tabler_rss24.h` |
| Subscriptions | Tabler `rss`, 24px | FreshRSS dashboard | `src/components/icons/tabler_rss24.h` |
| RSS home entry | Tabler `rss`, 32px | Home menu | `src/components/icons/tabler_rss24.h` |
| Unread article | Filled dot primitive | Article rows | `LyraTheme.cpp` |
| Read article | Hollow dot primitive | Article rows | `LyraTheme.cpp` |

The Home menu icon required one small, deliberately narrow addition to the
theme surface: a new `UIIcon::Rss` enum value (`BaseTheme.h`) and one `case`
in `LyraTheme.cpp`'s `iconForName()`. This is not the kind of change
`SCOPE.md`'s "theming surface is frozen" warns about — that's about new
theme *variants*; `BaseTheme` and `RoundedRaffTheme`'s `drawButtonMenu()`
don't render menu icons at all (the `rowIcon` callback is accepted but
unused in both), so only `LyraTheme` needed the new case. The RSS list row
callback also remains theme-safe: Lyra renders it, while Classic and
RoundedRaff preserve their existing row layouts.

## Configuring FreshRSS

FreshRSS API access must be enabled on the server. Create a dedicated FreshRSS
API password rather than using the normal web password. Configure the account
from **Reader → FreshRSS Settings**, or place this JSON at:

```
/.crosspoint/freshrss.json
```

```json
{
  "api_url": "https://reader.example/api/greader.php",
  "username": "reader-user",
  "password": "dedicated-api-password"
}
```

`api_url` may be a FreshRSS host, its `/api` directory, or the final
`greader.php` endpoint; firmware normalizes all three forms and adds the
Google Reader `/reader/api/0/` prefix to read-only requests. Plaintext
`password` is accepted only for import and is rewritten as a hardware-key
obfuscated `password_obf` value after loading. Credentials are kept in RAM only
for the active refresh session.

The global article limit is selectable as 200, 500, or 1000 (default 200).
These are the only FreshRSS cache-size choices; old settings values are treated
as deprecated and reset to 200. The selected limit is an on-device
admission bound, not a request to fetch linked article pages.
Continuation pages are followed until that limit or until FreshRSS has no more
articles. The device never sends read, star, tag, subscription, or other
mutation requests. Read/star state is local and namespaced by the FreshRSS
article ID.

## Architecture

| Concern | Files |
|---|---|
| FreshRSS Google Reader API and bounded JSON parser | `src/FreshRssApiClient.{h,cpp}`, `lib/JsonParser/StreamingJsonParser.{h,cpp}` |
| HTML → structured, styled text | `lib/RssParser/HtmlRichText.{h,cpp}`, `RichText.h` |
| Account credentials | `src/FreshRssAccountStore.{h,cpp}` |
| FreshRSS metadata/body snapshot | `src/FreshRssCache.{h,cpp}` |
| Shared EPUB/RSS layout primitives | `lib/ReaderLayout/ReaderLayout.h` |
| Read/unread + local stars | `src/RssItemStateStore.{h,cpp}` |
| UI | `src/activities/browser/RssFeedListActivity.*`, `RssItemListActivity.*`, `src/activities/reader/RssArticleActivity.*` |

Modeled directly on the OPDS Book Browser (`lib/OpdsParser/`,
`src/activities/browser/OpdsBookBrowserActivity.*`) — same on-demand-WiFi,
streaming-XML-parse, scrollable-list shape — since that is the closest
existing feature in the codebase. `ReaderLayout/ReaderLayout.h` provides the
bounded common minimum-raggedness breaker, Gujarati indentation rule, and
justification calculation used by both `ParsedText` and `RssArticleActivity`;
each reader still owns its format-specific word storage, bidi, ruby, and HTML
adaptation.

FreshRSS snapshots are versioned (current v4) and committed at
`/.crosspoint/freshrss/snapshot.bin`; a temporary file and backup are used so a
failed refresh preserves the previous valid snapshot. v4 stores the last
successful modified timestamp, account identity, article limit, generation, and
modified timestamps in its bounded index. FreshRSS is the only RSS backend —
the direct RSS/Atom feed path (`feeds.json`, XML parsing) has been removed;
`RssItemCache`'s reader (`loadIndex`/`loadItemBody`) still exists and is still
tested, but nothing in the UI calls it anymore. Cache format details are
documented in [file-formats.md](file-formats.md#crosspointfreshrsssnapshotbin).

Snapshot version 4 prefixes every article record with its bounded byte length
and appends a committed index containing article keys, modified timestamps,
record offsets, record sizes, subscription indexes, truncation flags, and
bounded category masks.
Navigation reads the index without touching article bodies, list pages seek to
their records, and article opening seeks directly to one RichText body. v3,
v2, and v1 snapshots remain readable; a v1 snapshot is upgraded one bounded
article at a time into the same atomic replacement flow. If migration, storage,
or validation fails, the old snapshot remains in place.

### Rich text: bold, italic, alignment, justification

`HtmlRichText::convert()` (`lib/RssParser/HtmlRichText.cpp`) turns an item's
raw HTML body into a `RichText` — a sequence of `RichParagraph`s, each a flat
list of `StyledWord`s (`lib/RssParser/RichText.h`) rather than a flat string:

- `<b>`/`<strong>` and `<i>`/`<em>` set the `EpdFontFamily::BOLD`/`ITALIC`
  bits on every word inside them (nesting depth counters, so `<b><i>x</i></b>`
  correctly gets `BOLD|ITALIC`). A style change landing mid-word (rare —
  styling tags almost always wrap whole words) is handled via
  `StyledWord::continuesPrevious`, which tells the layout not to insert a
  space where there wasn't one in the source.
- `<center>`, or the substring `"center"` appearing in a block tag's
  `align`/`style` attribute (a deliberately dumb heuristic, not real CSS
  parsing — see the comment in `HtmlRichText.cpp`), sets that paragraph's
  `TextAlign` to `CENTER`. Anything without explicit HTML alignment gets
  `TextAlign::DEFAULT`.
- Block tags (`<p>`, `<div>`, `<li>`, `<br>`, headings, …) end the current
  paragraph, same as the old plain-text version's paragraph breaks.

`RssArticleActivity::wrapParagraph()` resolves `DEFAULT` against the user's
own `CrossPointSettings::paragraphAlignment` at layout time — the same
setting `TxtReaderActivity`/`EpubReaderActivity` already read — so RSS
articles follow whatever alignment preference the reader already has, and an
explicit `<center>` in the source always wins over it. `JUSTIFY` is real
justification, not the "treated as left-aligned" fallback
`TxtReaderActivity::renderPage()` uses for plain text: each wrapped line
tracks its natural (non-stretched) width and its count of stretchable
word-gaps, and `render()` distributes `(viewportWidth - naturalWidth) /
gapCount` extra pixels into every gap — the same per-gap-average approach
`ParsedText::computeJustifyExtra()` uses for EPUB body text, minus that
engine's hyphenation/RTL/CJK handling, which this bounded a feature doesn't
need. A paragraph's last line is never stretched, standard typographic
convention.

### Gujarati shaping

Feed names (`RssFeedStore::fromJson`) and item titles
(`RssItemListActivity::fetchFeed`) are free text that bypasses `ParsedText`'s
EPUB parse-time shaping entirely, so each is run through
`GujaratiIntegration::shapeLongUiString()` once — at load/fetch time, before
the result is cached — rather than on every render. This follows the same
modularity rule as the rest of the UI (`AGENTS.md`: "Use
`GujaratiIntegration::shapeUiString()` for any UI string that bypasses
ParsedText"), but calls the long-text variant rather than `shapeUiString()`
itself: `shapeUiString()` runs `GujaratiShaper::shape()` over the whole
string at once, and that function's codepoint buffer is sized for roughly
one line (`MAX_WORD_CPS` in `GujaratiShaper.cpp`) — a real article title can
run past that. `shapeLongUiString()` (added alongside this feature, in
`lib/GujaratiShaper/GujaratiIntegration.*`) splits on whitespace and shapes
each word individually via `shapeWord()` instead.

Article **bodies** don't need `shapeLongUiString()` at all — `HtmlRichText`
already tokenizes the body into `StyledWord`s, so the refresh sink calls
`GujaratiIntegration::shapeWord()` directly on each word's text, the exact
per-word call `ParsedText` uses at EPUB parse time. The shaped `RichText` is
written immediately into the new feed snapshot, so offline rereads do not
need to reshape it.

## Memory bounds

Sized for the ESP32-C3's ~380KB RAM with no PSRAM:

- A FreshRSS refresh admits at most the selected 200/500/1000 items. Each HTTP
  request asks for a fixed 25-item page (`FreshRssApiClient::fetchArticles`'s
  `requestPageSize`) — smaller than the configured server default because the
  device's HTTP timeout can be exceeded by the time a 100-item response
  begins streaming. The sink retains only the current bounded item; completed
  metadata and shaped body records are written to `snapshot.bin.tmp`
  immediately.
- Each body is capped at 32 KB. Oversized bodies are marked truncated and the
  available prefix is still cached. The reader stores word pointers in its line
  index rather than copying every word string, so pagination does not double
  the article text in RAM.
- Read-state and star-state track at most 200 compact item markers per feed
  (oldest evicted first) across at most 24 tracked feeds. The local queue adds
  at most 256 compact IDs per tracked feed. These stores remain bounded and are
  only used while RSS is active.
- One selected cached body is loaded at a time and keeps the hard 32 KB body
  ceiling. The activity holds only the current visible metadata page and one
  selected body; new snapshots retain all admitted records and do not use LRU
  body eviction.

## Porting / upstream note

This is intentionally not upstream-aligned the way the Gujarati work is (that
integration was written to be mergeable; this one is explicitly out of scope
per `SCOPE.md`). If you're porting CrossInk to a different CrossPoint fork,
copy `lib/RssParser/`, `src/FreshRss*`, and `src/Rss*` stores and activities,
then wire
`HomeMenuItem::RSS_READER` into that fork's `ActivityManager`/`HomeActivity`
equivalents following the pattern in `src/activities/ActivityManager.cpp` and
`src/activities/home/HomeActivity.cpp`.

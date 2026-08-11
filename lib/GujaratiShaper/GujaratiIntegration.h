#pragma once

#include <string>

/**
 * Thin integration surface for wiring Gujarati shaping into CrossPoint and forks.
 *
 * ParsedText and UI code should call these helpers instead of reaching into
 * GujaratiShaper directly — keeps the merge footprint small when porting to
 * CrossInk, inx, or other CrossPoint-based firmware.
 */
namespace GujaratiIntegration {

/** Shape a layout word in place when it contains Gujarati (no-op otherwise). */
void shapeWord(std::string& word);

/**
 * Shape a word that has already been UTF-8 sanitized. RSS rich-text words are
 * sanitized by HtmlRichText while they are built, so using this variant avoids
 * rescanning every Latin word during a large cache refresh.
 */
void shapeSanitizedWord(std::string& word);

/**
 * Shape a short UI string in place (status bar, chapter list, etc.).
 * Splits on whitespace and shapes each word individually, the same per-word
 * approach ParsedText uses at EPUB parse time, so no codepoint is dropped
 * regardless of string length. Prefer this name for single-line labels;
 * shapeLongUiString() is an identical implementation kept as a distinct name
 * for multi-paragraph callers (e.g. an RSS article body) where "long" reads
 * more clearly at the call site.
 */
void shapeUiString(std::string& text);

/**
 * Shape a UI string of unbounded length in place — a multi-sentence or
 * multi-paragraph block (e.g. an RSS article body) rather than a short
 * label. Splits on whitespace and shapes each word individually, same as
 * shapeUiString().
 */
void shapeLongUiString(std::string& text);

/** Returns true when the string contains Gujarati that was (or will be) shaped. */
bool containsGujarati(const std::string& text);

}  // namespace GujaratiIntegration

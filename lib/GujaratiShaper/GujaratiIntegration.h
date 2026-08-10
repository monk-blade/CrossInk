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
 * Delegates to GujaratiShaper::shape() directly over the whole string, whose
 * codepoint buffer is sized for roughly one line (MAX_WORD_CPS in
 * GujaratiShaper.cpp) — longer input is silently truncated. Safe for the
 * short, single-line strings this is meant for; use shapeLongUiString() for
 * anything that isn't bounded to a short phrase (e.g. an article body).
 */
void shapeUiString(std::string& text);

/**
 * Shape a UI string of unbounded length in place — a multi-sentence or
 * multi-paragraph block (e.g. an RSS article body) rather than a short
 * label. Splits on whitespace and shapes each word individually via
 * shapeWord(), the same per-word approach ParsedText uses at EPUB parse
 * time, so it isn't subject to shapeUiString()'s whole-string length limit.
 */
void shapeLongUiString(std::string& text);

/** Returns true when the string contains Gujarati that was (or will be) shaped. */
bool containsGujarati(const std::string& text);

}  // namespace GujaratiIntegration

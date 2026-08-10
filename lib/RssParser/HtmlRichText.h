#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "RichText.h"

// Converts a bounded HTML snippet (an RSS/Atom item body) into structured,
// paragraph-and-word text: strips tags, decodes the handful of entities that
// actually show up in feed content, and preserves <b>/<strong>,
// <i>/<em>, and centering (<center>, or "center" appearing in an align/
// style attribute) as data rather than throwing them away. This is
// deliberately not a general HTML parser (lib/Epub already has one, far too
// heavy for a bounded feed snippet) — malformed markup degrades to
// best-effort text, never a crash or unbounded loop, since the input is
// already length-bounded by RssParser before it reaches here.
namespace HtmlRichText {
// maxChars bounds the total accumulated word-text length across the whole
// result, not the raw HTML input length — a page thick with markup and thin
// on visible text would otherwise use its whole budget on tags.
using ProgressCallback = std::function<void(size_t inputBytes)>;
RichText convert(const std::string& html, size_t maxChars, const ProgressCallback& progress = {});
}  // namespace HtmlRichText

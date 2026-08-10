#pragma once

#include <cctype>
#include <cstring>

#include <string>

namespace RssDateFormatter {
namespace detail {

inline bool isDigit(const char c) { return c >= '0' && c <= '9'; }

inline void skipSpaces(const char*& cursor) {
  while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
}

inline bool readDigits(const char*& cursor, const int minDigits, const int maxDigits, int& value) {
  const char* start = cursor;
  value = 0;
  int count = 0;
  while (count < maxDigits && isDigit(*cursor)) {
    value = value * 10 + (*cursor - '0');
    ++cursor;
    ++count;
  }
  if (count < minDigits) {
    cursor = start;
    return false;
  }
  return true;
}

inline bool readMonth(const char*& cursor, int& month) {
  static constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; ++i) {
    if (cursor[0] != '\0' && cursor[1] != '\0' && cursor[2] != '\0' &&
        std::tolower(static_cast<unsigned char>(cursor[0])) == std::tolower(static_cast<unsigned char>(MONTHS[i][0])) &&
        std::tolower(static_cast<unsigned char>(cursor[1])) == std::tolower(static_cast<unsigned char>(MONTHS[i][1])) &&
        std::tolower(static_cast<unsigned char>(cursor[2])) == std::tolower(static_cast<unsigned char>(MONTHS[i][2]))) {
      month = i + 1;
      cursor += 3;
      return true;
    }
  }
  return false;
}

inline bool readTime(const char*& cursor, int& hour, int& minute) {
  if (!readDigits(cursor, 1, 2, hour) || *cursor != ':') return false;
  ++cursor;
  if (!readDigits(cursor, 2, 2, minute)) return false;
  if (hour > 23 || minute > 59) return false;
  return true;
}

inline std::string fallback(const std::string& raw) {
  std::string compact;
  compact.reserve(raw.size());
  bool previousWasSpace = true;
  for (const char c : raw) {
    const bool space = std::isspace(static_cast<unsigned char>(c));
    if (space) {
      if (!previousWasSpace) compact += ' ';
    } else {
      compact += c;
    }
    previousWasSpace = space;
    if (compact.size() >= 48) break;
  }
  while (!compact.empty() && compact.back() == ' ') compact.pop_back();
  return compact;
}

}  // namespace detail

// Convert common RSS/Atom timestamps such as
// "Sun, 26 Jul 2026 02:30:00 +0200" and "2026-07-26T02:30:00+02:00"
// to a compact reader-friendly form. The original timezone is intentionally
// omitted because the device has no reliable feed-timezone conversion here.
inline std::string format(const std::string& raw) {
  const char* cursor = raw.c_str();
  detail::skipSpaces(cursor);

  int day = 0;
  int month = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
  bool hasTime = false;
  bool parsed = false;

  // ISO 8601: YYYY-MM-DD[ T]HH:MM[:SS]...
  if (detail::isDigit(cursor[0]) && detail::isDigit(cursor[1]) && detail::isDigit(cursor[2]) &&
      detail::isDigit(cursor[3]) && cursor[4] == '-') {
    const char* iso = cursor;
    parsed = detail::readDigits(cursor, 4, 4, year) && *cursor == '-';
    if (parsed) {
      ++cursor;
      parsed = detail::readDigits(cursor, 2, 2, month) && *cursor == '-';
    }
    if (parsed) {
      ++cursor;
      parsed = detail::readDigits(cursor, 2, 2, day);
    }
    if (parsed && (*cursor == 'T' || *cursor == 't' || std::isspace(static_cast<unsigned char>(*cursor)))) {
      ++cursor;
      detail::skipSpaces(cursor);
      hasTime = detail::readTime(cursor, hour, minute);
    }
    if (!parsed) cursor = iso;
  }

  // RFC 822/1123: [weekday, ]DD Mon YYYY HH:MM[:SS] ...
  if (!parsed) {
    const char* comma = std::strchr(cursor, ',');
    if (comma != nullptr) cursor = comma + 1;
    detail::skipSpaces(cursor);
    parsed = detail::readDigits(cursor, 1, 2, day);
    if (parsed) {
      detail::skipSpaces(cursor);
      parsed = detail::readMonth(cursor, month);
    }
    if (parsed) {
      detail::skipSpaces(cursor);
      parsed = detail::readDigits(cursor, 2, 4, year);
      if (year < 100) year += year >= 70 ? 1900 : 2000;
    }
    if (parsed) {
      detail::skipSpaces(cursor);
      hasTime = detail::readTime(cursor, hour, minute);
    }
  }

  if (!parsed || day < 1 || day > 31 || month < 1 || month > 12 || year < 1970 || year > 2200) {
    return detail::fallback(raw);
  }

  static constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::string result = std::to_string(day);
  result += ' ';
  result += MONTHS[month - 1];
  result += ' ';
  result += std::to_string(year);
  if (hasTime) {
    const int hour12 = hour % 12 == 0 ? 12 : hour % 12;
    result += ", ";
    result += std::to_string(hour12);
    result += ':';
    if (minute < 10) result += '0';
    result += std::to_string(minute);
    result += hour < 12 ? " AM" : " PM";
  }
  return result;
}

// A shorter list-row variant. Keep the day/month and time, but omit the year
// to save horizontal space; the full human-readable form remains available
// for feeds spanning multiple years.
inline std::string formatCompact(const std::string& raw) {
  const std::string result = format(raw);
  const size_t firstSpace = result.find(' ');
  const size_t yearSpace = firstSpace == std::string::npos ? std::string::npos : result.find(' ', firstSpace + 1);
  if (yearSpace == std::string::npos || yearSpace + 5 > result.size()) return result;
  for (size_t i = yearSpace + 1; i < yearSpace + 5; ++i) {
    if (!detail::isDigit(result[i])) return result;
  }
  const size_t comma = result.find(',', yearSpace + 1);
  return comma == std::string::npos ? result.substr(0, yearSpace) : result.substr(0, yearSpace) + result.substr(comma);
}

// A very compact list-row form. Keep the day/month and time, remove the
// comma, and use a one-letter meridiem suffix so the tiny RSS date font has
// enough room on narrow screens.
inline std::string formatListCompact(const std::string& raw) {
  std::string result = formatCompact(raw);
  const size_t comma = result.find(',');
  if (comma != std::string::npos) result.erase(comma, 1);
  if (result.size() >= 3) {
    const size_t suffix = result.size() - 3;
    if (result.compare(suffix, 3, " AM") == 0) {
      result.replace(suffix, 3, "a");
    } else if (result.compare(suffix, 3, " PM") == 0) {
      result.replace(suffix, 3, "p");
    }
  }
  return result;
}

}  // namespace RssDateFormatter

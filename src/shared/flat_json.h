#pragma once

// flat_json.h — strict parser for the flat JSON object schema shared by the bootstrapper application's
// branding loader and the make-setup packager: one object whose values are all strings. Unknown keys are
// accepted so newer files keep working with older binaries; any structural error fails the whole document
// so partial data can never produce a mixed result.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace huxerui_installer {

namespace detail {

class FlatJsonParser {
public:
  explicit FlatJsonParser(std::string_view text) : text_(text) {}

  bool Parse(std::map<std::string, std::string>& fields) {
    SkipWhitespace();
    if (!Consume('{')) {
      return false;
    }
    SkipWhitespace();
    if (Consume('}')) {
      return AtEnd();
    }
    for (;;) {
      std::string key;
      std::string value;
      if (!ParseString(key)) {
        return false;
      }
      SkipWhitespace();
      if (!Consume(':')) {
        return false;
      }
      SkipWhitespace();
      if (!ParseString(value)) {
        return false;
      }
      fields[std::move(key)] = std::move(value);
      SkipWhitespace();
      if (Consume('}')) {
        return AtEnd();
      }
      if (!Consume(',')) {
        return false;
      }
      SkipWhitespace();
    }
  }

private:
  bool AtEnd() {
    SkipWhitespace();
    return position_ == text_.size();
  }

  void SkipWhitespace() {
    while (position_ < text_.size()) {
      const char current = text_[position_];
      if (current != ' ' && current != '\t' && current != '\r' && current != '\n') {
        break;
      }
      ++position_;
    }
  }

  bool Consume(char expected) {
    if (position_ >= text_.size() || text_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool ParseString(std::string& out) {
    if (!Consume('"')) {
      return false;
    }
    out.clear();
    while (position_ < text_.size()) {
      const char current = text_[position_++];
      if (current == '"') {
        return true;
      }
      if (current == '\\') {
        if (!ParseEscape(out)) {
          return false;
        }
        continue;
      }
      if (static_cast<unsigned char>(current) < 0x20U) {
        return false;
      }
      out.push_back(current);
    }
    return false;
  }

  bool ParseEscape(std::string& out) {
    if (position_ >= text_.size()) {
      return false;
    }
    const char escaped = text_[position_++];
    switch (escaped) {
    case '"':
    case '\\':
    case '/':
      out.push_back(escaped);
      return true;
    case 'b':
      out.push_back('\b');
      return true;
    case 'f':
      out.push_back('\f');
      return true;
    case 'n':
      out.push_back('\n');
      return true;
    case 'r':
      out.push_back('\r');
      return true;
    case 't':
      out.push_back('\t');
      return true;
    case 'u':
      return ParseUnicodeEscape(out);
    default:
      return false;
    }
  }

  bool ParseHex4(std::uint32_t& value) {
    if (text_.size() - position_ < 4) {
      return false;
    }
    value = 0;
    for (int digit = 0; digit < 4; ++digit) {
      const char current = text_[position_++];
      value <<= 4U;
      if (current >= '0' && current <= '9') {
        value |= static_cast<std::uint32_t>(current - '0');
      } else if (current >= 'a' && current <= 'f') {
        value |= static_cast<std::uint32_t>(current - 'a' + 10);
      } else if (current >= 'A' && current <= 'F') {
        value |= static_cast<std::uint32_t>(current - 'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  }

  bool ParseUnicodeEscape(std::string& out) {
    std::uint32_t code_point = 0;
    if (!ParseHex4(code_point)) {
      return false;
    }
    if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
      if (text_.size() - position_ < 2 || text_[position_] != '\\' || text_[position_ + 1] != 'u') {
        return false;
      }
      position_ += 2;
      std::uint32_t low = 0;
      if (!ParseHex4(low) || low < 0xDC00U || low > 0xDFFFU) {
        return false;
      }
      code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
    } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
      return false;
    }
    AppendUtf8(out, code_point);
    return true;
  }

  static void AppendUtf8(std::string& out, std::uint32_t code_point) {
    if (code_point < 0x80U) {
      out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800U) {
      out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
      out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point < 0x10000U) {
      out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
      out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
  }

  std::string_view text_;
  std::size_t position_ = 0;
};

} // namespace detail

/// Parses one flat JSON object with string values into `fields`; returns false on any structural error.
inline bool ParseFlatJsonObject(std::string_view text, std::map<std::string, std::string>& fields) {
  return detail::FlatJsonParser(text).Parse(fields);
}

} // namespace huxerui_installer

#include "branding.h"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

namespace huxerui_installer {

namespace {

constexpr std::string_view kBrandingFileName = "branding.json";
constexpr std::size_t kMaxLicenseBytes = 512 * 1024;

void LogLine(const std::string& message) {
  const std::string line = "huxerui-installer: " + message + "\n";
  const int length = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0);
  if (length <= 0) {
    OutputDebugStringA(line.c_str());
    return;
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), wide.data(), length);
  OutputDebugStringW(wide.c_str());
}

std::filesystem::path ExecutableDirectory() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size() - 1) {
      return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::optional<std::string> ReadTextFile(const std::filesystem::path& path, std::size_t maximum_bytes) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  std::string content(maximum_bytes + 1, '\0');
  stream.read(content.data(), static_cast<std::streamsize>(content.size()));
  content.resize(static_cast<std::size_t>(stream.gcount()));
  if (content.size() > maximum_bytes) {
    return std::nullopt;
  }
  if (content.rfind("\xEF\xBB\xBF", 0) == 0) {
    content.erase(0, 3);
  }
  return content;
}

/// Strict parser for the branding schema: one flat JSON object whose values are all strings. Unknown keys
/// are accepted so newer bundles keep working with older BA executables; any structural error fails the
/// whole document so partial branding can never produce a mixed identity.
class BrandingJsonParser {
public:
  explicit BrandingJsonParser(std::string_view text) : text_(text) {}

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

int HexDigit(char current) {
  if (current >= '0' && current <= '9') {
    return current - '0';
  }
  if (current >= 'a' && current <= 'f') {
    return current - 'a' + 10;
  }
  if (current >= 'A' && current <= 'F') {
    return current - 'A' + 10;
  }
  return -1;
}

std::optional<huxerui::Color> ParseAccentColor(std::string_view value) {
  std::string_view hex = value;
  if (!hex.empty() && hex.front() == '#') {
    hex.remove_prefix(1);
  }
  if (hex.size() == 3) {
    std::string expanded;
    expanded.reserve(6);
    for (const char digit : hex) {
      expanded.push_back(digit);
      expanded.push_back(digit);
    }
    hex = expanded;
  }
  if (hex.size() != 6 && hex.size() != 8) {
    return std::nullopt;
  }
  int channels[4] = {0, 0, 0, 255};
  const std::size_t count = hex.size() / 2;
  for (std::size_t index = 0; index < count; ++index) {
    const int high = HexDigit(hex[index * 2]);
    const int low = HexDigit(hex[index * 2 + 1]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    channels[index] = high * 16 + low;
  }
  return huxerui::Color::Rgb(channels[0], channels[1], channels[2], static_cast<float>(channels[3]) / 255.0F);
}

} // namespace

Branding LoadBranding() {
  Branding branding;
  branding.executable_dir = ExecutableDirectory();
  if (branding.executable_dir.empty()) {
    LogLine("could not locate the executable directory; using built-in branding");
    return branding;
  }

  const std::filesystem::path json_path = branding.executable_dir / kBrandingFileName;
  const std::optional<std::string> json = ReadTextFile(json_path, 64 * 1024);
  if (!json) {
    LogLine("no readable branding.json next to the executable; using built-in branding");
    return branding;
  }

  std::map<std::string, std::string> fields;
  if (!BrandingJsonParser(*json).Parse(fields)) {
    LogLine("branding.json is malformed; using built-in branding");
    return branding;
  }
  const auto field = [&fields](const char* name) -> std::string_view {
    const auto found = fields.find(name);
    return found == fields.end() ? std::string_view{} : std::string_view{found->second};
  };
  if (const std::string_view value = field("productName"); !value.empty()) {
    branding.product_name = value;
  }
  if (const std::string_view value = field("publisher"); !value.empty()) {
    branding.publisher = value;
  }
  branding.logo_path = field("logoPath");
  branding.accent_color = field("accentColor");
  branding.license_file = field("licenseFile");

  if (!branding.accent_color.empty()) {
    branding.accent = ParseAccentColor(branding.accent_color);
    if (!branding.accent) {
      LogLine("branding accentColor is not a valid hex color; ignoring it");
    }
  }
  if (!branding.logo_path.empty()) {
    const std::filesystem::path logo_file = (branding.executable_dir / branding.logo_path).lexically_normal();
    try {
      branding.logo = huxerui::ImageAsset::FromFile(logo_file);
    } catch (const std::exception&) {
      LogLine("branding logo could not be loaded; drawing the default mark instead");
    }
  }
  if (!branding.license_file.empty()) {
    const std::filesystem::path license_path =
        (branding.executable_dir / branding.license_file).lexically_normal();
    if (std::optional<std::string> text = ReadTextFile(license_path, kMaxLicenseBytes)) {
      branding.license_text = std::move(*text);
    } else {
      LogLine("branding licenseFile could not be read; skipping the license page");
    }
  }
  return branding;
}

const Branding& GetBranding() {
  static const Branding branding = LoadBranding();
  return branding;
}

} // namespace huxerui_installer

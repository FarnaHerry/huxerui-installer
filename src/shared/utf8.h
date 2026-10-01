#pragma once

// utf8.h — UTF-8 <-> UTF-16 conversion helpers shared by the packager core, the CLI, and the studio GUI.
// std::filesystem::path narrow-string assignment on Windows uses the ANSI codepage, never UTF-8, so every
// path crossing the UI/core boundary goes through these helpers.

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace huxerui_installer {

inline std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int length =
      WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr,
                      nullptr);
  return result;
}

inline std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
  return result;
}

inline std::filesystem::path Utf8Path(std::string_view value) {
  return std::filesystem::path{Utf8ToWide(value)};
}

inline std::string PathToUtf8(const std::filesystem::path& value) {
  return WideToUtf8(value.wstring());
}

} // namespace huxerui_installer

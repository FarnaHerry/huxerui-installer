#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <huxerui/color.h>
#include <huxerui/resource.h>

namespace huxerui_installer {

/// Per-application branding injected at runtime from `branding.json` sitting next to the running
/// bootstrapper application executable.
///
/// The generic BA exe ships unbranded; the WiX Burn bundle carries `branding.json` (plus the referenced
/// logo image and license text file) as payloads, so Burn extracts them beside the BA exe before it runs.
/// `LoadBranding()` resolves every field against that directory and always succeeds: a missing or malformed
/// `branding.json` yields the generic defaults instead of failing the installation.
///
/// `branding.json` is a flat JSON object with optional string values only:
/// @code{.json}
/// {
///   "productName": "My App",
///   "publisher": "My Company",
///   "logoPath": "branding\\logo.png",
///   "accentColor": "#1F6FEB",
///   "licenseFile": "branding\\license.txt"
/// }
/// @endcode
struct Branding {
  /// Display name used across the installer UI. Defaults to a generic placeholder.
  std::string product_name = "HuxerUI Application";
  /// Publisher name reserved for surfaces that show it. Defaults to a generic placeholder.
  std::string publisher = "HuxerUI";
  /// Raw `logoPath` value relative to the executable directory; empty when unset.
  std::string logo_path;
  /// Raw `accentColor` value (`#RGB`, `#RRGGBB`, or `#RRGGBBAA`); empty when unset.
  std::string accent_color;
  /// Raw `licenseFile` value relative to the executable directory; empty when unset.
  std::string license_file;

  /// Directory containing the running BA executable; the payload root all relative paths resolve against.
  std::filesystem::path executable_dir;
  /// Logo loaded from `logo_path`; `HasValue()` is false when unset or unreadable.
  huxerui::ImageAsset logo;
  /// Parsed `accent_color`; `std::nullopt` when unset or not a valid hex color.
  std::optional<huxerui::Color> accent;
  /// Text loaded from `license_file`; empty when unset or unreadable.
  std::string license_text;
};

/// Loads branding from `branding.json` beside the running executable, resolving the logo, accent color,
/// and license text. Missing files and malformed JSON fall back to defaults with a debug log line; this
/// function never throws for bad input.
Branding LoadBranding();

/// Returns the process-wide branding, loaded on first call. Composition and the `Application` declaration
/// both read this value, so the JSON file is parsed exactly once per process.
const Branding& GetBranding();

} // namespace huxerui_installer

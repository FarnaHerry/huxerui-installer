#pragma once

// packager.h — packaging core shared by the make-setup.exe CLI and the GUI studio.
//
// RunPackaging() renders the parameterized wix/*.wxs.in templates, stages the bootstrapper application
// payloads and builds the MSI plus Burn bundle with a WiX v5 toolset, turning a staged application
// payload into <app>-<version>-windows-x86_64-setup.exe. All failures throw PackageError; progress and
// wix.exe output are streamed to the caller through the log callback (invoked on the calling thread).

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace huxerui_installer {

struct PackageOptions {
  /// Display name of the application (also the MSI file base name and install directory name). Required.
  std::string app_name;
  /// Stable reverse-domain application id; seeds the derived upgrade codes and the registry key. Required.
  std::string app_id;
  /// Semantic version without a leading v, for example 1.2.0. Required.
  std::string version;
  /// Directory whose contents become the installed files (the application payload). Required.
  std::filesystem::path application_dir;
  /// Application executable file name inside application_dir, used for shortcuts. Required.
  std::string app_exe;
  /// Path to the application .ico used by the bundle, the MSI, and the shortcuts. Required.
  std::filesystem::path icon;
  /// Path to branding.json; sibling files it references (logo, license) ride as BA payloads. Optional.
  std::filesystem::path branding_json;
  /// Manufacturer shown by the bundle and MSI. Defaults to the branding publisher, then app_id.
  std::string publisher;
  /// Directory with the prebuilt bootstrapper application. Defaults to `ba` next to the running executable.
  std::filesystem::path ba_dir;
  /// Explicit upgrade codes; both default to RFC 4122 v5 UUIDs derived from app_id (matching CMake's
  /// string(UUID ... TYPE SHA1)).
  std::string msi_upgrade_code;
  std::string bundle_upgrade_code;
  /// Output path of the setup executable. Defaults to ./<app>-<version>-windows-x86_64-setup.exe.
  std::filesystem::path output;
  /// wix.exe to run. Defaults to the `wix-toolset` directory shipped next to the running executable.
  std::filesystem::path wix_exe;
  /// Build scratch directory. Defaults to make-setup-work next to the output.
  std::filesystem::path work_dir;
};

struct PackageResult {
  std::filesystem::path setup;
  std::filesystem::path msi;
  std::string msi_upgrade_code;
  std::string bundle_upgrade_code;
};

/// Any validation, staging or WiX failure; what() is a user-readable single-line message.
class PackageError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/// Validates `options`, fills in defaults, and builds the setup executable. `log` receives one
/// human-readable line per call — progress messages plus wix.exe output. May be called from any thread;
/// the callback is always invoked on that same thread.
PackageResult RunPackaging(const PackageOptions& options, const std::function<void(std::string_view)>& log);

} // namespace huxerui_installer

// make_setup.cpp — native prebuilt-mode packaging entry point of huxerui-installer.
//
// A drop-in replacement for scripts/make-setup.ps1 that needs no PowerShell: a thin CLI over the shared
// packaging core (packager.cpp), which renders the wix/*.wxs.in templates and builds the MSI plus Burn
// bundle with the WiX toolset shipped beside it in the release package. Behavior (derived upgrade codes,
// payload layout, bindpaths) matches make-setup.ps1 exactly, so the entry points are interchangeable.
//
// The only external requirement is a .NET 6+ runtime for wix.exe itself.
//
//   make-setup.exe --app-name niki --app-id dev.niki --version 1.2.0 --app-dir .\dist\windows
//       --app-exe niki.exe --icon .\packaging\app.ico --branding .\packaging\branding.json
//       [--publisher "niki authors"] [--output .\niki-setup.exe]

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

#include "packager.h"

#include "utf8.h"


namespace {

namespace fs = std::filesystem;

using huxerui_installer::PackageError;
using huxerui_installer::PackageOptions;
using huxerui_installer::PackageResult;
using huxerui_installer::RunPackaging;
using huxerui_installer::PathToUtf8;
using huxerui_installer::Utf8Path;
using huxerui_installer::Utf8ToWide;
using huxerui_installer::WideToUtf8;

void PrintUsage() {
  std::cout <<
      R"(make-setup — package a staged HuxerUI application into a Burn bundle setup executable.

Required:
  --app-name <name>       Display name (also MSI base name and install directory name)
  --app-id <id>           Stable reverse-domain id; seeds the derived upgrade codes
  --version <x.y.z>       Semantic version without a leading v
  --app-dir <dir>         Directory whose contents become the installed files
  --app-exe <file>        Application executable file name inside --app-dir
  --icon <file.ico>       Icon used by the bundle, the MSI, and the shortcuts

Optional:
  --branding <file.json>  branding.json; referenced logo/license files ride as BA payloads
  --publisher <name>      Manufacturer (defaults to the branding publisher, then --app-id)
  --ba-dir <dir>          Prebuilt bootstrapper application directory (default: ba\ next to this exe)
  --wix-exe <file>        wix.exe to run (default: wix-toolset\ shipped next to this exe)
  --msi-upgrade-code <guid>     Pin explicit upgrade codes instead of the AppId-derived ones
  --bundle-upgrade-code <guid>
  --output <file>         Output setup path (default: .\<app>-<version>-windows-x86_64-setup.exe)
  --work-dir <dir>        Build scratch directory (default: make-setup-work next to the output)
  --help                  Show this text
)";
}

[[noreturn]] void UsageError(const std::string& message) {
  std::cerr << "make-setup: " << message << '\n';
  std::exit(1);
}

PackageOptions ParseOptions(int argc, wchar_t* argv[]) {
  PackageOptions options;
  std::map<std::string, std::string> values;
  for (int index = 1; index < argc; ++index) {
    std::string argument = WideToUtf8(argv[index]);
    if (argument == "--help" || argument == "-h" || argument == "-?") {
      PrintUsage();
      std::exit(0);
    }
    if (argument.rfind("--", 0) != 0) {
      UsageError("unexpected argument '" + argument + "'; use --help");
    }
    std::string key;
    std::string value;
    if (const std::size_t equals = argument.find('='); equals != std::string::npos) {
      key = argument.substr(2, equals - 2);
      value = argument.substr(equals + 1);
    } else {
      key = argument.substr(2);
      if (index + 1 >= argc) {
        UsageError("option --" + key + " expects a value");
      }
      value = WideToUtf8(argv[++index]);
    }
    values[std::move(key)] = std::move(value);
  }
  const auto take = [&values](const char* key) -> std::string {
    const auto found = values.find(key);
    if (found == values.end()) {
      return {};
    }
    std::string value = std::move(found->second);
    values.erase(found);
    return value;
  };
  const auto take_path = [&take](const char* key) -> fs::path {
    const std::string value = take(key);
    return value.empty() ? fs::path{} : fs::path{Utf8ToWide(value)};
  };

  options.app_name = take("app-name");
  options.app_id = take("app-id");
  options.version = take("version");
  options.application_dir = take_path("app-dir");
  options.app_exe = take("app-exe");
  options.icon = take_path("icon");
  options.branding_json = take_path("branding");
  options.publisher = take("publisher");
  options.ba_dir = take_path("ba-dir");
  options.msi_upgrade_code = take("msi-upgrade-code");
  options.bundle_upgrade_code = take("bundle-upgrade-code");
  options.output = take_path("output");
  options.wix_exe = take_path("wix-exe");
  options.work_dir = take_path("work-dir");
  if (!values.empty()) {
    UsageError("unknown option --" + values.begin()->first + "; use --help");
  }
  if (options.app_name.empty()) UsageError("--app-name is required; use --help");
  if (options.app_id.empty()) UsageError("--app-id is required; use --help");
  if (options.version.empty()) UsageError("--version is required; use --help");
  if (options.application_dir.empty()) UsageError("--app-dir is required; use --help");
  if (options.app_exe.empty()) UsageError("--app-exe is required; use --help");
  if (options.icon.empty()) UsageError("--icon is required; use --help");
  return options;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  const PackageOptions options = ParseOptions(argc, argv);
  try {
    const PackageResult result = RunPackaging(options, [](std::string_view line) { std::cout << line << '\n'; });
    std::cout << "Setup:             " << WideToUtf8(result.setup.wstring()) << '\n'
              << "Msi:               " << WideToUtf8(result.msi.wstring()) << '\n'
              << "MsiUpgradeCode:    " << result.msi_upgrade_code << '\n'
              << "BundleUpgradeCode: " << result.bundle_upgrade_code << '\n';
  } catch (const PackageError& error) {
    std::cerr << "make-setup: " << error.what() << '\n';
    return 2;
  }
  return 0;
}

// make_setup.cpp — native prebuilt-mode packaging entry point of huxerui-installer.
//
// A drop-in replacement for scripts/make-setup.ps1 that needs no PowerShell: it renders the
// parameterized wix/*.wxs.in templates, builds the MSI and the Burn bundle with the WiX toolset shipped
// beside it in the release package, and produces <AppName>-<Version>-windows-x86_64-setup.exe from a
// staged application payload. Behavior (derived upgrade codes, payload layout, bindpaths) matches
// make-setup.ps1 exactly, so the two entry points are interchangeable.
//
// The only external requirement is a .NET 6+ runtime for wix.exe itself.
//
//   make-setup.exe --app-name niki --app-id dev.niki --version 1.2.0 --app-dir .\dist\windows
//       --app-exe niki.exe --icon .\packaging\app.ico --branding .\packaging\branding.json
//       [--publisher "niki authors"] [--output .\niki-setup.exe]

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flat_json.h"

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kBaExeName[] = L"huxerui-installer-ba.exe";

struct Options {
  std::string app_name;
  std::string app_id;
  std::string version;
  fs::path application_dir;
  std::string app_exe;
  fs::path icon;
  fs::path branding_json;
  std::string publisher;
  fs::path ba_dir;
  std::string msi_upgrade_code;
  std::string bundle_upgrade_code;
  fs::path output;
  fs::path wix_exe;
  fs::path work_dir;
};

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

std::string WideToUtf8(std::wstring_view value) {
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

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
  return result;
}

[[noreturn]] void Fail(const std::string& message) {
  std::cerr << "make-setup: " << message << '\n';
  std::exit(2);
}

fs::path ExecutableDirectory() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      Fail("could not locate the executable directory");
    }
    if (length < buffer.size() - 1) {
      return fs::path(buffer.data(), buffer.data() + length).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

bool IsSemVersion(std::string_view value) {
  int components = 0;
  std::size_t digits = 0;
  for (const char current : value) {
    if (current == '.') {
      if (digits == 0) {
        return false;
      }
      ++components;
      digits = 0;
      continue;
    }
    if (current < '0' || current > '9') {
      return false;
    }
    ++digits;
  }
  return components == 2 && digits > 0;
}

Options ParseOptions(int argc, wchar_t* argv[]) {
  Options options;
  std::map<std::string, std::string> values;
  for (int index = 1; index < argc; ++index) {
    std::string argument = WideToUtf8(argv[index]);
    if (argument == "--help" || argument == "-h" || argument == "-?") {
      PrintUsage();
      std::exit(0);
    }
    if (argument.rfind("--", 0) != 0) {
      Fail("unexpected argument '" + argument + "'; use --help");
    }
    std::string key;
    std::string value;
    if (const std::size_t equals = argument.find('='); equals != std::string::npos) {
      key = argument.substr(2, equals - 2);
      value = argument.substr(equals + 1);
    } else {
      key = argument.substr(2);
      if (index + 1 >= argc) {
        Fail("option --" + key + " expects a value");
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
    Fail("unknown option --" + values.begin()->first + "; use --help");
  }
  for (const char* required : {"app-name", "app-id", "version", "app-exe"}) {
    const std::string* field = nullptr;
    if (std::string_view{required} == "app-name") field = &options.app_name;
    if (std::string_view{required} == "app-id") field = &options.app_id;
    if (std::string_view{required} == "version") field = &options.version;
    if (std::string_view{required} == "app-exe") field = &options.app_exe;
    if (field && field->empty()) {
      Fail(std::string{"--"} + required + " is required; use --help");
    }
  }
  if (options.application_dir.empty()) Fail("--app-dir is required; use --help");
  if (options.icon.empty()) Fail("--icon is required; use --help");
  return options;
}

std::string ReadTextFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    Fail("could not read " + WideToUtf8(path.wstring()));
  }
  std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  if (content.rfind("\xEF\xBB\xBF", 0) == 0) {
    content.erase(0, 3);
  }
  return content;
}

void WriteTextFile(const fs::path& path, const std::string& content) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    Fail("could not write " + WideToUtf8(path.wstring()));
  }
  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// RFC 4122 version 5 (SHA1) UUID over the namespace used by CMake's string(UUID ... TYPE SHA1):
// 6ba7b810-9dad-11d1-80b4-00c04fd430c8. Matches make-setup.ps1 and CMake for the same name.
std::string DeriveNameGuid(const std::string& name) {
  static constexpr std::uint8_t kNamespace[16] = {0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
                                                  0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) {
    Fail("BCryptOpenAlgorithmProvider(SHA1) failed");
  }
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::uint8_t digest[20];
  const bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
                  BCryptHashData(hash, const_cast<std::uint8_t*>(kNamespace), sizeof(kNamespace), 0) == 0 &&
                  BCryptHashData(hash, reinterpret_cast<std::uint8_t*>(const_cast<char*>(name.data())),
                                 static_cast<ULONG>(name.size()), 0) == 0 &&
                  BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
  if (hash) {
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) {
    Fail("SHA1 hashing failed");
  }
  digest[6] = (digest[6] & 0x0FU) | 0x50U;  // version 5
  digest[8] = (digest[8] & 0x3FU) | 0x80U;  // RFC 4122 variant
  // Format in RFC 4122 network byte order so the result matches CMake's string(UUID ... TYPE SHA1).
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(36);
  for (int index = 0; index < 16; ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      result.push_back('-');
    }
    result.push_back(kHex[digest[index] >> 4U]);
    result.push_back(kHex[digest[index] & 0x0FU]);
  }
  return result;
}

std::string XmlEscape(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char current : value) {
    switch (current) {
    case '&': result += "&amp;"; break;
    case '<': result += "&lt;"; break;
    case '>': result += "&gt;"; break;
    case '"': result += "&quot;"; break;
    default: result.push_back(current);
    }
  }
  return result;
}

std::string RenderTemplate(const fs::path& template_path, const std::map<std::string, std::string>& tokens) {
  std::string content = ReadTextFile(template_path);
  for (const auto& [token, value] : tokens) {
    std::size_t position = 0;
    while ((position = content.find(token, position)) != std::string::npos) {
      content.replace(position, token.size(), value);
      position += value.size();
    }
  }
  if (content.find("@@") != std::string::npos) {
    Fail("unresolved @@TOKEN@@ left in " + WideToUtf8(template_path.filename().wstring()));
  }
  return content;
}

std::wstring QuoteArg(const std::wstring& value) {
  return L'"' + value + L'"';
}

// Runs wix.exe with the console handles inherited; returns its exit code.
DWORD RunWix(const fs::path& wix_exe, const std::vector<std::wstring>& arguments) {
  std::wstring command_line = QuoteArg(wix_exe.wstring());
  for (const std::wstring& argument : arguments) {
    command_line += L' ';
    command_line += QuoteArg(argument);
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                      &process)) {
    Fail("could not start " + WideToUtf8(wix_exe.wstring()) +
         " (a .NET 6+ runtime is required); error " + std::to_string(GetLastError()));
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return exit_code;
}

void RequireFile(const fs::path& path, const std::string& what) {
  std::error_code error;
  if (!fs::is_regular_file(path, error)) {
    Fail(what + " does not exist: " + WideToUtf8(path.wstring()));
  }
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  Options options = ParseOptions(argc, argv);

  if (!IsSemVersion(options.version)) {
    Fail("--version must be MAJOR.MINOR.PATCH without a leading v, got '" + options.version + "'");
  }

  const fs::path package_root = ExecutableDirectory();
  if (options.ba_dir.empty()) {
    options.ba_dir = package_root / L"ba";
  }
  if (options.wix_exe.empty()) {
    options.wix_exe = package_root / L"wix-toolset" / L"tools" / L"net6.0" / L"any" / L"wix.exe";
  }
  const fs::path ba_exe = options.ba_dir / kBaExeName;
  RequireFile(ba_exe, "prebuilt bootstrapper application is incomplete; missing");
  RequireFile(options.ba_dir / L"mbanative.dll", "prebuilt bootstrapper application is incomplete; missing");
  std::error_code error;
  if (!fs::is_directory(options.application_dir, error)) {
    Fail("--app-dir does not exist: " + WideToUtf8(options.application_dir.wstring()));
  }
  RequireFile(options.application_dir / Utf8ToWide(options.app_exe),
              "--app-exe '" + options.app_exe + "' not found inside --app-dir:");
  RequireFile(options.icon, "--icon");
  RequireFile(options.wix_exe, "WiX toolset executable");

  // ---------------------------------------------------------------- branding ----
  std::map<std::string, std::string> branding;
  if (!options.branding_json.empty()) {
    RequireFile(options.branding_json, "--branding");
    if (!huxerui_installer::ParseFlatJsonObject(ReadTextFile(options.branding_json), branding)) {
      Fail("branding.json is malformed: " + WideToUtf8(options.branding_json.wstring()));
    }
  }
  if (options.publisher.empty()) {
    options.publisher = branding["publisher"];
  }
  if (options.publisher.empty()) {
    options.publisher = options.app_id;
  }

  // ------------------------------------------------------------- upgrade ids ----
  if (options.msi_upgrade_code.empty()) {
    options.msi_upgrade_code = DeriveNameGuid(options.app_id + ".msi");
  }
  if (options.bundle_upgrade_code.empty()) {
    options.bundle_upgrade_code = DeriveNameGuid(options.app_id + ".bundle");
  }

  // ------------------------------------------------------------------ layout ----
  if (options.output.empty()) {
    options.output = fs::current_path() /
        Utf8ToWide(options.app_name + "-" + options.version + "-windows-x86_64-setup.exe");
  }
  options.output = fs::absolute(options.output);
  if (options.work_dir.empty()) {
    options.work_dir = options.output.parent_path() / L"make-setup-work";
  }
  const fs::path project_dir = options.work_dir / L"project";      // !(bindpath.Project): icon and project files
  const fs::path installer_stage = options.work_dir / L"installer";  // !(bindpath.Installer): BA exe + payloads
  const fs::path wix_source_dir = options.work_dir / L"wxs";
  const fs::path msi_path = options.work_dir / Utf8ToWide(options.app_name + ".msi");
  const fs::path payload_fragment = options.work_dir / L"installer-payloads.wxs";

  fs::remove_all(options.work_dir, error);
  fs::create_directories(project_dir);
  fs::create_directories(installer_stage);
  fs::create_directories(wix_source_dir);

  const std::wstring icon_file = options.icon.filename().wstring();
  fs::copy_file(options.icon, project_dir / icon_file, fs::copy_options::overwrite_existing);

  fs::copy(options.ba_dir, installer_stage,
           fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
  if (error) {
    Fail("could not stage the bootstrapper application: " + error.message());
  }
  if (!options.branding_json.empty()) {
    // branding.json at the BA executable root; referenced payloads (logoPath, licenseFile) keep their
    // relative paths so LoadBranding() resolves them next to the executable.
    const fs::path branding_dir = fs::absolute(options.branding_json).parent_path();
    fs::copy_file(options.branding_json, installer_stage / L"branding.json",
                  fs::copy_options::overwrite_existing);
    for (const char* key : {"logoPath", "licenseFile"}) {
      const auto found = branding.find(key);
      if (found == branding.end() || found->second.empty()) {
        continue;
      }
      const fs::path relative = Utf8ToWide(found->second);
      const fs::path source = branding_dir / relative;
      RequireFile(source, std::string{"branding."} + key + " points at a missing file:");
      const fs::path target = installer_stage / relative;
      fs::create_directories(target.parent_path());
      fs::copy_file(source, target, fs::copy_options::overwrite_existing);
    }
  }

  // ---------------------------------------------------------------- templates ---
  const std::map<std::string, std::string> tokens = {
      {"@@APP_NAME@@", XmlEscape(options.app_name)},
      {"@@MANUFACTURER@@", XmlEscape(options.publisher)},
      {"@@APP_VERSION@@", options.version},
      {"@@APP_ID@@", XmlEscape(options.app_id)},
      {"@@APP_EXE@@", XmlEscape(options.app_exe)},
      {"@@MSI_UPGRADE_CODE@@", options.msi_upgrade_code},
      {"@@BUNDLE_UPGRADE_CODE@@", options.bundle_upgrade_code},
      {"@@ICON_FILE@@", XmlEscape(WideToUtf8(icon_file))},
      {"@@INSTALL_DIR_NAME@@", XmlEscape(options.app_name)},
      {"@@BA_EXE@@", WideToUtf8(kBaExeName)},
  };
  for (const wchar_t* template_name : {L"Package.wxs.in", L"Bundle.wxs.in"}) {
    const fs::path template_path = package_root / L"wix" / template_name;
    RequireFile(template_path, "WiX template");
    std::wstring rendered_name = template_name;
    rendered_name.resize(rendered_name.size() - 3);  // strip ".in"
    WriteTextFile(wix_source_dir / rendered_name, RenderTemplate(template_path, tokens));
  }

  // -------------------------------------------------------------------- WiX -----
  std::wcout << L"Building " << msi_path.filename().wstring() << L'\n';
  DWORD exit_code = RunWix(options.wix_exe,
                           {L"build", (wix_source_dir / L"Package.wxs").wstring(), L"-arch", L"x64",
                            L"-bindpath", L"Application=" + options.application_dir.wstring(),
                            L"-bindpath", L"Project=" + project_dir.wstring(), L"-out", msi_path.wstring()});
  if (exit_code != 0) {
    Fail("wix build Package.wxs failed with exit code " + std::to_string(exit_code));
  }

  // Payload fragment: every BA staging file except the BA executable itself rides as a bundle payload
  // (mirrors HuxerUI's cmake/HuxerUIGenerateWixPayloads.cmake; the PayloadGroup id is a contract with
  // Bundle.wxs.in).
  const fs::path ba_exe_staged = fs::weakly_canonical(installer_stage / kBaExeName);
  std::vector<fs::path> payload_files;
  for (fs::recursive_directory_iterator it(installer_stage), end; it != end; ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    if (_wcsicmp(fs::weakly_canonical(it->path()).wstring().c_str(), ba_exe_staged.wstring().c_str()) == 0) {
      continue;
    }
    payload_files.push_back(it->path());
  }
  std::sort(payload_files.begin(), payload_files.end(),
            [&installer_stage](const fs::path& left, const fs::path& right) {
              return fs::relative(left, installer_stage).wstring() < fs::relative(right, installer_stage).wstring();
            });
  std::string fragment;
  fragment += "<Wix xmlns=\"http://wixtoolset.org/schemas/v4/wxs\">\r\n  <Fragment>\r\n"
              "    <PayloadGroup Id=\"HuxerUIInstallerPayloads\">\r\n";
  for (const fs::path& file : payload_files) {
    fragment += "      <Payload SourceFile=\"" + XmlEscape(WideToUtf8(fs::absolute(file).wstring())) +
                "\" Name=\"" + XmlEscape(WideToUtf8(fs::relative(file, installer_stage).wstring())) +
                "\" />\r\n";
  }
  fragment += "    </PayloadGroup>\r\n  </Fragment>\r\n</Wix>\r\n";
  WriteTextFile(payload_fragment, fragment);

  std::wcout << L"Building " << options.output.filename().wstring() << L'\n';
  exit_code = RunWix(options.wix_exe,
                     {L"build", (wix_source_dir / L"Bundle.wxs").wstring(), payload_fragment.wstring(), L"-arch",
                      L"x64", L"-bindpath", L"Installer=" + installer_stage.wstring(), L"-bindpath",
                      L"Package=" + options.work_dir.wstring(), L"-bindpath", L"Project=" + project_dir.wstring(),
                      L"-out", options.output.wstring()});
  if (exit_code != 0) {
    Fail("wix build Bundle.wxs failed with exit code " + std::to_string(exit_code));
  }
  if (!fs::is_regular_file(options.output, error)) {
    Fail("wix reported success but the bundle is missing: " + WideToUtf8(options.output.wstring()));
  }

  std::cout << "Setup:             " << WideToUtf8(options.output.wstring()) << '\n'
            << "Msi:               " << WideToUtf8(msi_path.wstring()) << '\n'
            << "MsiUpgradeCode:    " << options.msi_upgrade_code << '\n'
            << "BundleUpgradeCode: " << options.bundle_upgrade_code << '\n';
  return 0;
}

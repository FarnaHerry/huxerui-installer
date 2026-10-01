#include "packager.h"

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "flat_json.h"
#include "utf8.h"

namespace huxerui_installer {

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kBaExeName[] = L"huxerui-installer-ba.exe";

[[noreturn]] void Fail(const std::string& message) {
  throw PackageError(message);
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

// Runs wix.exe with stdout/stderr captured into a pipe, forwarding lines to the log callback as they
// arrive. Returns the exit code.
DWORD RunWix(const fs::path& wix_exe, const std::vector<std::wstring>& arguments,
             const std::function<void(std::string_view)>& log) {
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &security, 0)) {
    Fail("could not create the wix.exe output pipe");
  }
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  std::wstring command_line = QuoteArg(wix_exe.wstring());
  for (const std::wstring& argument : arguments) {
    command_line += L' ';
    command_line += QuoteArg(argument);
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = write_end;
  startup.hStdError = write_end;
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                      &process)) {
    const DWORD error = GetLastError();
    CloseHandle(read_end);
    CloseHandle(write_end);
    Fail("could not start " + WideToUtf8(wix_exe.wstring()) + " (a .NET 6+ runtime is required); error " +
         std::to_string(error));
  }
  CloseHandle(write_end);

  std::string pending;
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(read_end, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    pending.append(buffer, read);
    std::size_t newline = 0;
    while ((newline = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, newline);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!line.empty()) {
        log(line);
      }
      pending.erase(0, newline + 1);
    }
  }
  if (!pending.empty()) {
    if (pending.back() == '\r') {
      pending.pop_back();
    }
    if (!pending.empty()) {
      log(pending);
    }
  }
  CloseHandle(read_end);

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

PackageResult RunPackaging(const PackageOptions& options, const std::function<void(std::string_view)>& log) {
  if (options.app_name.empty()) Fail("app name is required");
  if (options.app_id.empty()) Fail("app id is required");
  if (options.app_exe.empty()) Fail("app executable name is required");
  if (!IsSemVersion(options.version)) {
    Fail("version must be MAJOR.MINOR.PATCH without a leading v, got '" + options.version + "'");
  }

  const fs::path package_root = ExecutableDirectory();
  const fs::path ba_dir = options.ba_dir.empty() ? package_root / L"ba" : options.ba_dir;
  const fs::path wix_exe = options.wix_exe.empty()
                               ? package_root / L"wix-toolset" / L"tools" / L"net6.0" / L"any" / L"wix.exe"
                               : options.wix_exe;
  const fs::path ba_exe = ba_dir / kBaExeName;
  RequireFile(ba_exe, "prebuilt bootstrapper application is incomplete; missing");
  RequireFile(ba_dir / L"mbanative.dll", "prebuilt bootstrapper application is incomplete; missing");
  std::error_code error;
  if (!fs::is_directory(options.application_dir, error)) {
    Fail("application directory does not exist: " + WideToUtf8(options.application_dir.wstring()));
  }
  RequireFile(options.application_dir / Utf8ToWide(options.app_exe),
              "app executable '" + options.app_exe + "' not found inside the application directory:");
  RequireFile(options.icon, "icon");
  RequireFile(wix_exe, "WiX toolset executable");

  // ---------------------------------------------------------------- branding ----
  std::map<std::string, std::string> branding;
  if (!options.branding_json.empty()) {
    RequireFile(options.branding_json, "branding.json");
    if (!ParseFlatJsonObject(ReadTextFile(options.branding_json), branding)) {
      Fail("branding.json is malformed: " + WideToUtf8(options.branding_json.wstring()));
    }
  }
  std::string publisher = options.publisher;
  if (publisher.empty()) {
    publisher = branding["publisher"];
  }
  if (publisher.empty()) {
    publisher = options.app_id;
  }

  // ------------------------------------------------------------- upgrade ids ----
  PackageResult result;
  result.msi_upgrade_code = options.msi_upgrade_code.empty() ? DeriveNameGuid(options.app_id + ".msi")
                                                             : options.msi_upgrade_code;
  result.bundle_upgrade_code = options.bundle_upgrade_code.empty() ? DeriveNameGuid(options.app_id + ".bundle")
                                                                   : options.bundle_upgrade_code;

  // ------------------------------------------------------------------ layout ----
  const fs::path output = options.output.empty()
                              ? fs::current_path() /
                                    Utf8ToWide(options.app_name + "-" + options.version + "-windows-x86_64-setup.exe")
                              : fs::absolute(options.output);
  const fs::path work_dir = options.work_dir.empty() ? output.parent_path() / L"make-setup-work"
                                                     : options.work_dir;
  const fs::path project_dir = work_dir / L"project";        // !(bindpath.Project): icon and project files
  const fs::path installer_stage = work_dir / L"installer";  // !(bindpath.Installer): BA exe + payloads
  const fs::path wix_source_dir = work_dir / L"wxs";
  const fs::path msi_path = work_dir / Utf8ToWide(options.app_name + ".msi");
  const fs::path payload_fragment = work_dir / L"installer-payloads.wxs";

  fs::remove_all(work_dir, error);
  fs::create_directories(project_dir);
  fs::create_directories(installer_stage);
  fs::create_directories(wix_source_dir);

  const std::wstring icon_file = options.icon.filename().wstring();
  fs::copy_file(options.icon, project_dir / icon_file, fs::copy_options::overwrite_existing);

  fs::copy(ba_dir, installer_stage, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
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
      {"@@MANUFACTURER@@", XmlEscape(publisher)},
      {"@@APP_VERSION@@", options.version},
      {"@@APP_ID@@", XmlEscape(options.app_id)},
      {"@@APP_EXE@@", XmlEscape(options.app_exe)},
      {"@@MSI_UPGRADE_CODE@@", result.msi_upgrade_code},
      {"@@BUNDLE_UPGRADE_CODE@@", result.bundle_upgrade_code},
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
  log("Building " + WideToUtf8(msi_path.filename().wstring()));
  DWORD exit_code = RunWix(wix_exe,
                           {L"build", (wix_source_dir / L"Package.wxs").wstring(), L"-arch", L"x64",
                            L"-bindpath", L"Application=" + options.application_dir.wstring(), L"-bindpath",
                            L"Project=" + project_dir.wstring(), L"-out", msi_path.wstring()},
                           log);
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
                "\" Name=\"" + XmlEscape(WideToUtf8(fs::relative(file, installer_stage).wstring())) + "\" />\r\n";
  }
  fragment += "    </PayloadGroup>\r\n  </Fragment>\r\n</Wix>\r\n";
  WriteTextFile(payload_fragment, fragment);

  log("Building " + WideToUtf8(output.filename().wstring()));
  exit_code = RunWix(wix_exe,
                     {L"build", (wix_source_dir / L"Bundle.wxs").wstring(), payload_fragment.wstring(), L"-arch",
                      L"x64", L"-bindpath", L"Installer=" + installer_stage.wstring(), L"-bindpath",
                      L"Package=" + work_dir.wstring(), L"-bindpath", L"Project=" + project_dir.wstring(), L"-out",
                      output.wstring()},
                     log);
  if (exit_code != 0) {
    Fail("wix build Bundle.wxs failed with exit code " + std::to_string(exit_code));
  }
  if (!fs::is_regular_file(output, error)) {
    Fail("wix reported success but the bundle is missing: " + WideToUtf8(output.wstring()));
  }

  result.setup = output;
  result.msi = msi_path;
  return result;
}

} // namespace huxerui_installer

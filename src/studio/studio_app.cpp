// studio_app.cpp — GUI front end of the huxerui-installer packager.
//
// A thin HuxerUI shell over the shared packaging core (packager.cpp): a form for the application
// identity and payload paths, a Build button that runs RunPackaging() on a worker thread, and a live
// log of the wix.exe output. Ships as its own release asset (huxerui-installer-gui-<version>) next to
// the CLI package, so users pick the front end they prefer; both derive identical upgrade codes.

#include <windows.h>

#include <shellapi.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <huxerui/huxerui.h>

#include "packager.h"
#include "utf8.h"

using namespace huxerui;
using huxerui_installer::PackageError;
using huxerui_installer::PackageOptions;
using huxerui_installer::PackageResult;
using huxerui_installer::PathToUtf8;
using huxerui_installer::RunPackaging;
using huxerui_installer::Utf8Path;

namespace {

struct BuildState {
  bool busy = false;
  bool failed = false;
  std::string status = "Fill in the form, then build.";
  std::string log;
  std::string setup_path;
};

struct BuildOutcome {
  bool ok = false;
  std::string error;
  PackageResult result;
};

void AppendLog(BuildState& state, const std::string& line) {
  state.log += line;
  state.log += '\n';
  // Keep the tail so a chatty wix.exe cannot grow the state without bound.
  constexpr std::size_t kMaxLogBytes = 256 * 1024;
  if (state.log.size() > kMaxLogBytes) {
    state.log.erase(0, state.log.size() - kMaxLogBytes);
  }
}

bool IsSemVersion(const std::string& value) {
  int components = 0;
  std::size_t digits = 0;
  for (const char current : value) {
    if (current == '.') {
      if (digits == 0) return false;
      ++components;
      digits = 0;
      continue;
    }
    if (current < '0' || current > '9') return false;
    ++digits;
  }
  return components == 2 && digits > 0;
}

View FormField(std::string label, State<TextEditingValue> value, std::string placeholder) {
  return TextField(value)
      .Label(std::move(label))
      .Placeholder(std::move(placeholder))
      .OnChanged([value](const TextEditingValue& next) mutable { value = next; });
}

[[huxerui::composable]]
View PickerField(std::string label, State<TextEditingValue> value, std::string placeholder, View picker_button) {
  const ThemeSpec& theme = UseTheme();
  return Column {
    Text(std::move(label), TextRole::Label).With(Foreground(theme.colors.on_surface_variant)),
    Flow {
      Text(value->text.empty() ? std::move(placeholder) : value->text, TextRole::Label)
          .With(Foreground(value->text.empty() ? theme.colors.on_surface_variant : theme.colors.on_surface)),
      std::move(picker_button),
    }.With(Spacing(theme.spacing.small), CrossAlign(CrossAxisAlignment::Center)),
  }.With(Spacing(theme.spacing.extra_small), CrossAlign(CrossAxisAlignment::Stretch));
}

[[huxerui::composable]]
View StudioContent() {
  const ThemeSpec& theme = UseTheme();
  auto tasks = UseTaskScope();
  auto picker = UseService<FilePicker>();

  auto app_name = UseState(TextEditingValue::FromText(""));
  auto app_id = UseState(TextEditingValue::FromText(""));
  auto version = UseState(TextEditingValue::FromText(""));
  auto publisher = UseState(TextEditingValue::FromText(""));
  auto app_dir = UseState(TextEditingValue::FromText(""));
  auto app_exe = UseState(TextEditingValue::FromText(""));
  auto icon = UseState(TextEditingValue::FromText(""));
  auto branding = UseState(TextEditingValue::FromText(""));
  auto output = UseState(TextEditingValue::FromText(""));
  auto build = UseState(BuildState{});

  const auto pick_directory = [picker, tasks](State<TextEditingValue> target) {
    return Button("Browse...").With(Enabled(picker->CanOpenDirectories())).OnClick([=]() mutable {
      tasks.Launch([=]() -> Task<void> {
        auto selected = co_await picker->OpenDirectoryAsync();
        if (selected) {
          if (const auto file = selected->AsFile()) {
            target = TextEditingValue::FromText(file->Path());
          }
        }
      });
    });
  };
  const auto pick_file = [picker, tasks](State<TextEditingValue> target, FilePickerFilter filter) {
    return Button("Browse...").With(Enabled(picker->CanOpenFiles())).OnClick([=]() mutable {
      tasks.Launch([=]() -> Task<void> {
        auto selected = co_await picker->OpenFileAsync(filter);
        if (selected) {
          if (const auto file = selected->AsFile()) {
            target = TextEditingValue::FromText(file->Path());
          }
        }
      });
    });
  };

  const bool form_valid = !app_name->text.empty() && !app_id->text.empty() &&
                          IsSemVersion(version->text) && !app_dir->text.empty() && !app_exe->text.empty() &&
                          !icon->text.empty();

  return Column {
    Text("HuxerUI Installer Studio", TextRole::Title),
    Text("Package a staged application payload into a Burn bundle setup executable. The WiX toolset and "
         "the prebuilt bootstrapper application ship next to this program."),
    Column {
      FormField("App name", app_name, "My App"),
      FormField("App ID (stable reverse-domain, seeds the upgrade codes)", app_id, "com.example.myapp"),
      Flow {
        FormField("Version", version, "1.2.0"),
        FormField("Publisher (optional)", publisher, "Defaults to the branding publisher"),
      }.With(Spacing(theme.spacing.medium)),
      PickerField("Application payload directory", app_dir, "No directory selected",
                  pick_directory(app_dir)),
      FormField("App executable (inside the payload directory)", app_exe, "myapp.exe"),
      PickerField("Icon (.ico)", icon, "No icon selected",
                  pick_file(icon, FilePickerFilter{.name = "Icons", .extensions = {"ico"}})),
      PickerField("branding.json (optional)", branding, "Generic branding",
                  pick_file(branding, FilePickerFilter{.name = "JSON", .extensions = {"json"}})),
      FormField("Output setup path (optional)", output, "<app>-<version>-windows-x86_64-setup.exe"),
    }.With(Spacing(theme.spacing.medium), CrossAlign(CrossAxisAlignment::Stretch)),
    Flow {
      Button("Build setup").With(Enabled(form_valid && !build->busy)).OnClick([=]() mutable {
        build = BuildState{true, false, "Building...", "", ""};
        PackageOptions options;
        options.app_name = app_name->text;
        options.app_id = app_id->text;
        options.version = version->text;
        options.publisher = publisher->text;
        options.application_dir = Utf8Path(app_dir->text);
        options.app_exe = app_exe->text;
        options.icon = Utf8Path(icon->text);
        options.branding_json = Utf8Path(branding->text);
        options.output = Utf8Path(output->text);
        tasks.Launch([=]() -> Task<void> {
          const BuildOutcome outcome = co_await RunWorker([options, tasks, build]() mutable {
            BuildOutcome outcome;
            try {
              outcome.result = RunPackaging(options, [tasks, build](std::string_view line) mutable {
                tasks.Post([build, line = std::string(line)]() mutable {
                  build.Update([&](BuildState& state) { AppendLog(state, line); });
                });
              });
              outcome.ok = true;
            } catch (const PackageError& error) {
              outcome.error = error.what();
            }
            return outcome;
          });
          if (outcome.ok) {
            const std::string path = PathToUtf8(outcome.result.setup);
            build.Update([&](BuildState& state) {
              state.busy = false;
              state.failed = false;
              state.status = "Build complete.";
              state.setup_path = path;
              AppendLog(state, "Setup: " + path);
            });
          } else {
            build.Update([&](BuildState& state) {
              state.busy = false;
              state.failed = true;
              state.status = "Build failed: " + outcome.error;
              AppendLog(state, "error: " + outcome.error);
            });
          }
        });
      }),
      Button("Open output folder").With(Enabled(!build->setup_path.empty())).OnClick([=] {
        const std::filesystem::path setup = Utf8Path(build->setup_path);
        ShellExecuteW(nullptr, L"open", setup.parent_path().wstring().c_str(), nullptr, nullptr, SW_SHOW);
      }),
    }.With(Spacing(theme.spacing.medium)),
    Text(build->status, TextRole::Label)
        .With(Foreground(build->failed ? theme.colors.error : theme.colors.primary)),
    ScrollView {
      SelectionArea {
        Text(build->log.empty() ? std::string{"Build output appears here."} : build->log, TextRole::Label),
      },
    }.With(
        ScrollBar(),
        Frame{.min_height = 180.0F},
        Padding(theme.spacing.medium),
        Background(theme.colors.surface_container_low),
        CornerRadius(theme.shapes.small)
    ),
  }.With(
      Padding(theme.spacing.extra_large),
      Spacing(theme.spacing.large),
      Background(theme.colors.background),
      CrossAlign(CrossAxisAlignment::Stretch)
  );
}

View App() {
  return MaterialTheme {StudioContent()};
}

} // namespace

const Application application{
    App,
    {
        .window = {
            .title = "HuxerUI Installer Studio",
            .initial_size = {880.0F, 760.0F},
        },
    }
};

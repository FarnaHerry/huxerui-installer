// Entry point of the generic HuxerUI bootstrapper application. Burn launches this executable as the
// bundle's UX process; RunInstallerApplication() connects to the engine, shows the composed installer
// interface (or drives a silent run when launched with Display.None), and returns its exit code.

#include <windows.h>

#include <huxerui/windows/installer.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
  return huxerui::windows::RunInstallerApplication();
}

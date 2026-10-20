
// Reinstall a TrueType font programmatically. Installs it as a user font.

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <minwindef.h>
#include <securitybaseapi.h>
#include <windows.h>
#include <winerror.h>
#include <wingdi.h>
#include <winnt.h>
#include <winreg.h>
#endif

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"
#include "fonts/ttf.h"

[[maybe_unused]]
static std::string GetFontName(std::string_view filename) {
  std::unique_ptr<TTF> ttf = TTF::Load(filename);
  CHECK(ttf.get() != nullptr) << "Unable to load TTF: " << filename;

  std::optional<std::string> name = ttf->FullName();
  CHECK(name.has_value()) << "Couldn't determine font name? " << filename;
  return name.value();
}

#ifdef _WIN32
static void Reinstall(std::string filename) {
  if (!Util::ExistsFile(filename)) {
    Print("Source file does not exist: {}\n", filename);
    return;
  }

  const char *local_appdata = getenv("LOCALAPPDATA");
  if (!local_appdata) {
    Print("LOCALAPPDATA environment variable not found.\n");
    return;
  }

  std::string fonts_dir = std::string(local_appdata) + "\\Microsoft\\Windows\\Fonts";

  std::string base = Util::FileOf(filename);
  std::string dest = fonts_dir + "\\" + base;
  Util::CreatePathFor(dest);

  RemoveFontResourceA(dest.c_str());

  if (Util::ExistsFile(dest)) {
    if (!Util::RemoveFile(dest)) {
      Util::BackupFile(dest);
    }
  }

  if (!Util::CopyFileBytes(filename, dest)) {
    Print("Failed to copy {} to {}\n", filename, dest);
    return;
  }

  if (AddFontResourceA(dest.c_str()) == 0) {
    Print("AddFontResourceA failed for {}\n", dest);
    return;
  }

  HKEY hkey;
  LSTATUS status = RegOpenKeyExA(
      HKEY_CURRENT_USER,
      "Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
      0, KEY_SET_VALUE, &hkey);

  if (status == ERROR_SUCCESS) {
    std::string font_name = GetFontName(filename);
    std::string val_name = font_name + " (TrueType)";
    RegSetValueExA(hkey, val_name.c_str(), 0, REG_SZ,
                   (const BYTE *)dest.c_str(), (DWORD)(dest.length() + 1));
    RegCloseKey(hkey);
  } else {
    Print("Warning: Failed to open registry key to save font.\n");
  }

  SendMessageTimeoutA(HWND_BROADCAST, WM_FONTCHANGE, 0, 0,
                      SMTO_ABORTIFHUNG, 1000, nullptr);

  Print("Successfully installed {}\n", base);
}

#elif defined(__APPLE__) && defined(__MACH__)

static void Reinstall(std::string filename) {
  if (!Util::ExistsFile(filename)) {
    Print("Source file does not exist: {}\n", filename);
    return;
  }

  // Sanity check that it's a valid TTF.
  std::unique_ptr<TTF> ttf = TTF::Load(filename);
  CHECK(ttf.get() != nullptr) << "Unable to load TTF: " << filename;

  const char *home = getenv("HOME");
  if (!home) {
    Print("HOME environment variable not found.\n");
    return;
  }

  std::string fonts_dir = std::string(home) + "/Library/Fonts";
  std::string base = Util::FileOf(filename);
  std::string dest = fonts_dir + "/" + base;
  Util::CreatePathFor(dest);

  if (Util::ExistsFile(dest)) {
    if (!Util::RemoveFile(dest)) {
      Util::BackupFile(dest);
    }
  }

  if (!Util::CopyFileBytes(filename, dest)) {
    Print("Failed to copy {} to {}\n", filename, dest);
    return;
  }

  Print("Successfully installed {}\n", base);
}

#else

#error Unsupported platform for reinstall

#endif


int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./reinstall.exe font.ttf\n\n"
    "Installs a single truetype font file, first uninstalling\n"
    "an existing one with the same filename if it is already\n"
    "installed.\n";

  Reinstall(argv[1]);

  return 0;
}

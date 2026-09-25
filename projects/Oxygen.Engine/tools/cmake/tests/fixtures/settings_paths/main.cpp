#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include "DemoShell/Services/SettingsService.h"

namespace fs = std::filesystem;
using oxygen::examples::SettingsService;
auto Require(bool condition, const char* message) -> void
{
  if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
  if (argc != 2) return 2;
  try {
    const auto root = fs::absolute(argv[1]);
    const auto original = root / "original SDK";
    const auto moved = root / "moved SDK";
    fs::create_directories(original);
    fs::create_directories(moved);
    const auto relative = fs::path("share/oxygen/Content/.cooked/container.index.bin");
    const auto external = root / "external content/model.glb";
    const auto sibling = root / "original SDK sibling/model.glb";
    {
      SettingsService settings(original / "settings.json", original);
      Require(settings.EncodePath(original / relative) == relative, "SDK path was not made relative");
      Require(settings.EncodePath(external) == external, "External path was changed");
      Require(settings.EncodePath(sibling) == sibling, "Sibling prefix incorrectly treated as SDK-local");
      Require(settings.EncodePath({}).empty(), "Empty path was changed");
      settings.SetString("index", settings.EncodePath(original / relative).generic_string());
      settings.SetString("external", settings.EncodePath(external).generic_string());
      settings.Save();
    }
    fs::copy_file(original / "settings.json", moved / "settings.json", fs::copy_options::overwrite_existing);
    {
      SettingsService settings(moved / "settings.json", moved);
      Require(settings.DecodePath(*settings.GetString("index")) == moved / relative, "Moved settings refer to old SDK");
      Require(settings.DecodePath(*settings.GetString("external")) == external, "External path was rebased");
      Require(settings.DecodePath({}).empty(), "Empty stored path was changed");
    }
    {
      SettingsService settings(root / "source-settings.json");
      Require(settings.EncodePath(relative) == relative, "Source-tree behavior changed");
      Require(settings.DecodePath(relative) == relative, "Source-tree resolution changed");
    }
    std::cout << "PASS: saved settings survive SDK relocation; external, sibling, empty and source-tree paths are preserved.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

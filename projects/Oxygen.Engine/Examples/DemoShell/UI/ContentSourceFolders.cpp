//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "DemoShell/UI/ContentSourceFolders.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::examples::ui {

namespace {
  //! The mounts file's object; a new one when the file is missing.
  auto ReadMountsFile(const std::filesystem::path& file) -> nlohmann::json
  {
    if (!std::filesystem::exists(file)) {
      return nlohmann::json {
        { "schema_version", content::import::kMountsSchemaVersion },
        { "mounts", nlohmann::json::object() },
      };
    }
    std::ifstream input(file);
    if (!input) {
      throw std::runtime_error("Cannot read " + file.string());
    }
    auto document = nlohmann::json::parse(input, nullptr, false);
    if (!document.is_object()) {
      throw std::runtime_error(file.string() + " is not a JSON object");
    }
    if (!document.contains("mounts") || !document.at("mounts").is_object()) {
      document["mounts"] = nlohmann::json::object();
    }
    return document;
  }

  auto WriteMountsFile(
    const std::filesystem::path& file, const nlohmann::json& document) -> void
  {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::trunc);
    output << document.dump(2) << '\n';
    if (!output) {
      throw std::runtime_error("Cannot write " + file.string());
    }
  }

  //! A character a mount name allows after its first (`[A-Za-z0-9_-]`).
  auto IsNameCharacter(const char ch) -> bool
  {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) != 0 || ch == '_' || ch == '-';
  }
} // namespace

auto LocalMountsFile(const std::filesystem::path& scratch_root)
  -> std::filesystem::path
{
  return scratch_root / content::import::kMountsFileName;
}

auto SourceFolderMountName(const std::filesystem::path& folder,
  const std::span<const std::string> taken) -> std::string
{
  auto base = std::string {};
  for (const char ch : folder.filename().string()) {
    base.push_back(IsNameCharacter(ch) ? ch : '_');
  }
  // A mount name starts with a letter or `_`.
  if (base.empty()
    || std::isalpha(static_cast<unsigned char>(base.front())) == 0) {
    base.insert(base.begin(), '_');
  }
  const auto is_taken = [taken](const std::string& name) -> bool {
    return std::ranges::contains(taken, name)
      || name == content::import::kEngineMountName;
  };
  auto name = base;
  for (auto suffix = 2; is_taken(name); ++suffix) {
    name = base + "_" + std::to_string(suffix);
  }
  return name;
}

auto AddLocalMount(const std::filesystem::path& file,
  const std::string_view name, const std::filesystem::path& folder) -> void
{
  auto document = ReadMountsFile(file);
  document["mounts"][std::string(name)] = folder.generic_string();
  WriteMountsFile(file, document);
}

auto RemoveLocalMount(
  const std::filesystem::path& file, const std::string_view name) -> void
{
  auto document = ReadMountsFile(file);
  document["mounts"].erase(std::string(name));
  WriteMountsFile(file, document);
}

} // namespace oxygen::examples::ui

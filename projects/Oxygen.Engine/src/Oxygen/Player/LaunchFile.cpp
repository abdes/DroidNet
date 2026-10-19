//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Player/LaunchFile.h>
#include <Oxygen/Player/SchemaCheck.h>

namespace oxygen::player {

namespace {

  using nlohmann::json;

  auto Resolve(const std::filesystem::path& base_dir, const std::string& text)
    -> std::filesystem::path
  {
    const auto path = std::filesystem::path(text);
    return (path.is_absolute() ? path : base_dir / path).lexically_normal();
  }

  auto IsPakFile(const std::filesystem::path& path) -> bool
  {
    auto extension = path.extension().string();
    std::ranges::transform(
      extension, extension.begin(), [](const unsigned char c) -> char {
        return static_cast<char>(std::tolower(c));
      });
    return extension == ".pak";
  }

  auto ReadMounts(const json& document, const std::filesystem::path& base_dir,
    const std::string_view source_name, PlayerLaunch& launch) -> void
  {
    for (const auto& entry : document.at("mounts")) {
      auto mount = LaunchMount {
        .kind = LaunchMount::Kind::kLooseCookedRoot,
        .path = Resolve(base_dir, entry.get<std::string>()),
      };
      if (IsPakFile(mount.path)) {
        mount.kind = LaunchMount::Kind::kPakFile;
        if (!std::filesystem::is_regular_file(mount.path)) {
          throw LaunchFileError(fmt::format("{}: PAK file '{}' does not exist",
            source_name, mount.path.string()));
        }
      } else if (!std::filesystem::is_directory(mount.path)) {
        throw LaunchFileError(
          fmt::format("{}: loose cooked root '{}' is not a folder", source_name,
            mount.path.string()));
      }
      launch.mounts.push_back(std::move(mount));
    }
  }

  auto ReadWindow(const json& window, LaunchWindow& out) -> void
  {
    if (const auto it = window.find("title"); it != window.end()) {
      out.title = it->get<std::string>();
    }
    if (const auto it = window.find("mode"); it != window.end()) {
      out.mode = it->get<std::string>() == "borderless"
        ? LaunchWindowMode::kBorderless
        : LaunchWindowMode::kWindowed;
    }
    if (const auto it = window.find("width"); it != window.end()) {
      out.width = it->get<std::uint32_t>();
    }
    if (const auto it = window.find("height"); it != window.end()) {
      out.height = it->get<std::uint32_t>();
    }
    if (const auto it = window.find("vsync"); it != window.end()) {
      out.vsync = it->get<bool>();
    }
    if (const auto it = window.find("frame_cap"); it != window.end()) {
      out.frame_cap = it->get<std::uint32_t>();
    }
  }

} // namespace

auto ParseLaunchFile(const std::string_view text,
  const std::filesystem::path& base_dir, const std::string_view source_name)
  -> PlayerLaunch
{
  auto document = json::parse(text, nullptr, false);
  if (document.is_discarded()) {
    throw LaunchFileError(
      fmt::format("{}: the launch file is not valid JSON", source_name));
  }
  if (const auto errors = LaunchSchema().Validate(document); !errors.empty()) {
    throw LaunchFileError(fmt::format("{}: the launch file does not match "
                                      "oxygen.player-launch.schema.json:\n  {}",
      source_name, fmt::join(errors, "\n  ")));
  }

  auto launch = PlayerLaunch {};
  ReadMounts(document, base_dir, source_name, launch);
  launch.scene = document.at("scene").get<std::string>();
  if (const auto it = document.find("script_source_roots");
    it != document.end()) {
    for (const auto& entry : *it) {
      auto root = Resolve(base_dir, entry.get<std::string>());
      if (!std::filesystem::is_directory(root)) {
        throw LaunchFileError(
          fmt::format("{}: script source root '{}' is not a folder",
            source_name, root.string()));
      }
      launch.script_source_roots.push_back(std::move(root));
    }
  }
  if (const auto it = document.find("window"); it != document.end()) {
    ReadWindow(*it, launch.window);
  }
  if (const auto it = document.find("control"); it != document.end()) {
    launch.control = LaunchControl {
      .port = it->value("port", std::uint16_t { 0U }),
      .exit_when_owner_leaves = it->value("exit_when_owner_leaves", false),
    };
  }
  return launch;
}

auto ReadLaunchFile(const std::filesystem::path& path) -> PlayerLaunch
{
  auto stream = std::ifstream(path, std::ios::binary);
  if (!stream) {
    throw LaunchFileError(
      fmt::format("cannot read launch file '{}'", path.string()));
  }
  const auto text = std::string(
    std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char> {});
  const auto absolute = std::filesystem::absolute(path);
  return ParseLaunchFile(text, absolute.parent_path(), path.string());
}

} // namespace oxygen::player

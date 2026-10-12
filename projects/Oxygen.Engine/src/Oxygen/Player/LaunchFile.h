//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oxygen::player {

//! A launch file that cannot be read, is not valid JSON, breaks the schema or
//! names content that does not exist. The message says which and where.
class LaunchFileError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

//! One cooked content source.
struct LaunchMount {
  enum class Kind : std::uint8_t {
    kLooseCookedRoot,
    kPakFile,
  };

  Kind kind { Kind::kLooseCookedRoot };
  //! Absolute.
  std::filesystem::path path;
};

struct LaunchWindow {
  std::string title { "Oxygen Player" };
  //! Client size in pixels.
  std::uint32_t width { 1280U };
  std::uint32_t height { 720U };
  bool vsync { true };
  //! Frames per second; 0 is off.
  std::uint32_t frame_cap { 0U };
};

//! The whole input of a player run, read from its launch file.
struct PlayerLaunch {
  //! Highest precedence last.
  std::vector<LaunchMount> mounts;
  //! Scene virtual path.
  std::string scene;
  //! Absolute; non-empty enables source reload.
  std::vector<std::filesystem::path> script_source_roots;
  LaunchWindow window;
  //! Pipe name without the `\\.\pipe\` prefix.
  std::optional<std::string> diagnostics_pipe;
};

//! Parses launch file text; relative paths resolve against `base_dir`.
/*!
 The text must validate against `oxygen.player-launch.schema.json`, and
 every mount and script source root must exist.

 \throws LaunchFileError naming `source_name` and the failing value.
*/
[[nodiscard]] auto ParseLaunchFile(std::string_view text,
  const std::filesystem::path& base_dir, std::string_view source_name)
  -> PlayerLaunch;

//! Reads and parses a launch file; relative paths resolve against its folder.
/*!
 \throws LaunchFileError
*/
[[nodiscard]] auto ReadLaunchFile(const std::filesystem::path& path)
  -> PlayerLaunch;

//! Whether the loader can replace these mounts in place between sessions:
//! true when every mount is a loose cooked root.
[[nodiscard]] auto CanRemount(const PlayerLaunch& launch) noexcept -> bool;

} // namespace oxygen::player

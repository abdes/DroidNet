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

#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::player {

//! A launch file that cannot be read, is not valid JSON, breaks the schema or
//! names content that does not exist. The message says which and where.
class LaunchFileError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

//! One cooked content source: a loose cooked root or a pak file.
using LaunchMount = content::ContentMount;

//! How the player window shows.
enum class LaunchWindowMode : std::uint8_t {
  //! In a frame, at the launch size.
  kWindowed,
  //! Over its display at the desktop resolution, without a frame.
  kBorderless,
};

struct LaunchWindow {
  std::string title { "Oxygen Player" };
  LaunchWindowMode mode { LaunchWindowMode::kWindowed };
  //! Framebuffer size in physical pixels, while windowed.
  std::uint32_t width { 1280U };
  std::uint32_t height { 720U };
  bool vsync { true };
  //! Frames per second; 0 is off.
  std::uint32_t frame_cap { 0U };
};

//! The launch file's control server settings.
struct LaunchControl {
  //! Loopback TCP port; 0 takes an ephemeral one.
  std::uint16_t port { 0U };
  //! Stop when the owning client leaves, or when none connects in time.
  bool exit_when_owner_leaves { false };
};

//! The whole input of a player run, read from its launch file.
struct PlayerLaunch {
  //! Highest precedence last.
  std::vector<LaunchMount> mounts;
  //! Scene virtual path.
  std::string scene;
  //! Source mount folders by name, absolute; external scripts resolve through
  //! them, and a non-empty table enables source reload.
  PathFinderConfig::SourceMountTable source_mounts;
  LaunchWindow window;
  //! Starts the control server when present.
  std::optional<LaunchControl> control;
};

//! Parses launch file text; relative paths resolve against `base_dir`.
/*!
 The text must validate against `oxygen.player-launch.schema.json`, and
 every mount and source mount folder must exist.

 \throws LaunchFileError naming `source_name` and the failing value.
*/
[[nodiscard]] auto ParseLaunchFile(std::string_view text,
  const std::filesystem::path& base_dir, std::string_view source_name)
  -> PlayerLaunch;

//! The most bytes a launch file may hold.
inline constexpr serio::ByteCount kMaxLaunchFileBytes {
  uint64_t { 1024 } * 1024,
};

//! Reads and parses a launch file; relative paths resolve against its folder.
/*!
 \throws LaunchFileError when the file cannot be read, is larger than
 `kMaxLaunchFileBytes` or is not a valid launch file.
*/
[[nodiscard]] auto ReadLaunchFile(const std::filesystem::path& path)
  -> PlayerLaunch;

} // namespace oxygen::player

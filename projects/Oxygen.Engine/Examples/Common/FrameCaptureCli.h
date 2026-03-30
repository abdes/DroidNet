//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/GraphicsConfig.h>

namespace oxygen::examples::cli {

struct FrameCaptureCliState {
  std::string provider = "off";
  std::string load = "attached";
  std::string library;
  std::string output;
  uint64_t from_frame = 0;
  uint32_t frame_count = 0;
};

inline auto NormalizeCliToken(std::string value) -> std::string
{
  std::ranges::transform(value, value.begin(),
    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

inline auto ParseCaptureProvider(std::string value)
  -> oxygen::FrameCaptureProvider
{
  value = NormalizeCliToken(std::move(value));

  if (value == "off" || value == "none") {
    return oxygen::FrameCaptureProvider::kNone;
  }
  if (value == "renderdoc") {
    return oxygen::FrameCaptureProvider::kRenderDoc;
  }
  if (value == "pix") {
    return oxygen::FrameCaptureProvider::kPix;
  }

  throw std::runtime_error("Invalid value for --capture-provider. Expected "
                           "one of: off, renderdoc, pix");
}

inline auto ParseCaptureLoadMode(std::string value)
  -> oxygen::FrameCaptureInitMode
{
  value = NormalizeCliToken(std::move(value));

  if (value == "attached") {
    return oxygen::FrameCaptureInitMode::kAttachedOnly;
  }
  if (value == "search") {
    return oxygen::FrameCaptureInitMode::kSearchPath;
  }
  if (value == "path") {
    return oxygen::FrameCaptureInitMode::kExplicitPath;
  }

  throw std::runtime_error("Invalid value for --capture-load. Expected one of:"
                           " attached, search, path");
}

inline auto MakeCaptureOptions(FrameCaptureCliState& state)
  -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Capture options");
  options->Add(clap::Option::WithKey("capture-provider")
      .About("Frame capture provider to enable: off, renderdoc, or pix")
      .Long("capture-provider")
      .WithValue<std::string>()
      .DefaultValue(std::string("off"))
      .UserFriendlyName("provider")
      .StoreTo(&state.provider)
      .Build());
  return options;
}

inline auto MakeAdvancedCaptureOptions(FrameCaptureCliState& state)
  -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Advanced capture options");
  options->Add(clap::Option::WithKey("capture-load")
      .About("How to locate the capture runtime: attached, search, or path. "
             "RenderDoc only for now.")
      .Long("capture-load")
      .WithValue<std::string>()
      .DefaultValue(std::string("attached"))
      .UserFriendlyName("mode")
      .StoreTo(&state.load)
      .Build());
  options->Add(clap::Option::WithKey("capture-library")
      .About("Explicit capture runtime library path used when "
             "--capture-load=path. RenderDoc only for now.")
      .Long("capture-library")
      .WithValue<std::string>()
      .UserFriendlyName("path")
      .StoreTo(&state.library)
      .Build());
  options->Add(clap::Option::WithKey("capture-output")
      .About("Capture file path template. RenderDoc only for now.")
      .Long("capture-output")
      .WithValue<std::string>()
      .UserFriendlyName("template")
      .StoreTo(&state.output)
      .Build());
  options->Add(clap::Option::WithKey("capture-from-frame")
      .About("Zero-based first rendered frame to capture. Frame 0 is the "
             "first rendered frame. RenderDoc only for now.")
      .Long("capture-from-frame")
      .WithValue<uint64_t>()
      .DefaultValue(uint64_t { 0 })
      .UserFriendlyName("frame")
      .StoreTo(&state.from_frame)
      .Build());
  options->Add(clap::Option::WithKey("capture-frame-count")
      .About("Number of consecutive frames to capture starting at "
             "--capture-from-frame. RenderDoc only for now.")
      .Long("capture-frame-count")
      .WithValue<uint32_t>()
      .DefaultValue(uint32_t { 0 })
      .UserFriendlyName("count")
      .StoreTo(&state.frame_count)
      .Build());
  return options;
}

inline auto BuildFrameCaptureConfig(const FrameCaptureCliState& state,
  const bool using_headless_backend = false) -> oxygen::FrameCaptureConfig
{
  const auto provider = ParseCaptureProvider(state.provider);
  if (provider == oxygen::FrameCaptureProvider::kNone) {
    return {};
  }

  if (using_headless_backend) {
    throw std::runtime_error("--capture-provider requires a Direct3D12 run. "
                             "Disable --headless or set --capture-provider "
                             "off.");
  }

  if (state.from_frame != 0 && state.frame_count == 0) {
    throw std::runtime_error("--capture-frame-count must be greater than 0 "
                             "when --capture-from-frame is set");
  }

  if (provider == oxygen::FrameCaptureProvider::kPix) {
    if (NormalizeCliToken(state.load) != "attached" || !state.library.empty()
      || !state.output.empty() || state.from_frame != 0
      || state.frame_count != 0) {
      throw std::runtime_error("--capture-load, --capture-library, "
                               "--capture-output, --capture-from-frame, and "
                               "--capture-frame-count are currently supported "
                               "only for RenderDoc");
    }
    return oxygen::FrameCaptureConfig { .provider = provider };
  }

  const auto init_mode = ParseCaptureLoadMode(state.load);
  if (init_mode == oxygen::FrameCaptureInitMode::kExplicitPath
    && state.library.empty()) {
    throw std::runtime_error(
      "--capture-library is required when --capture-load=path");
  }

  return oxygen::FrameCaptureConfig {
    .provider = provider,
    .init_mode = init_mode,
    .from_frame = state.from_frame,
    .frame_count = state.frame_count,
    .module_path = state.library,
    .capture_file_template = state.output,
  };
}

inline auto LogCaptureOptions(const FrameCaptureCliState& state) -> void
{
  LOG_F(INFO, "Parsed capture-provider option = {}", state.provider);
  LOG_F(INFO, "Parsed capture-load option = {}", state.load);
  if (!state.library.empty()) {
    LOG_F(INFO, "Parsed capture-library option = {}", state.library);
  }
  if (!state.output.empty()) {
    LOG_F(INFO, "Parsed capture-output option = {}", state.output);
  }
  if (state.frame_count != 0 || state.from_frame != 0) {
    LOG_F(INFO, "Parsed capture-from-frame option = {}", state.from_frame);
    LOG_F(INFO, "Parsed capture-frame-count option = {}", state.frame_count);
  }
}

} // namespace oxygen::examples::cli

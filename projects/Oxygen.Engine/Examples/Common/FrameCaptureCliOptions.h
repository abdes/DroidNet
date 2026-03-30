//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>

#include "FrameCaptureCli.h"

namespace oxygen::examples::cli {

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
      .About("How to locate the capture runtime: attached, search, or path")
      .Long("capture-load")
      .WithValue<std::string>()
      .DefaultValue(std::string("attached"))
      .UserFriendlyName("mode")
      .StoreTo(&state.load)
      .Build());
  options->Add(clap::Option::WithKey("capture-library")
      .About("Explicit capture runtime library path used when "
             "--capture-load=path")
      .Long("capture-library")
      .WithValue<std::string>()
      .UserFriendlyName("path")
      .StoreTo(&state.library)
      .Build());
  options->Add(clap::Option::WithKey("capture-output")
      .About("Capture file path template")
      .Long("capture-output")
      .WithValue<std::string>()
      .UserFriendlyName("template")
      .StoreTo(&state.output)
      .Build());
  options->Add(clap::Option::WithKey("capture-from-frame")
      .About("Zero-based first rendered frame to capture. Frame 0 is the "
             "first rendered frame. PIX startup capture requires a value "
             "greater than 0 when --capture-frame-count is set")
      .Long("capture-from-frame")
      .WithValue<uint64_t>()
      .DefaultValue(uint64_t { 0 })
      .UserFriendlyName("frame")
      .StoreTo(&state.from_frame)
      .Build());
  options->Add(clap::Option::WithKey("capture-frame-count")
      .About("Number of consecutive frames to capture starting at "
             "--capture-from-frame")
      .Long("capture-frame-count")
      .WithValue<uint32_t>()
      .DefaultValue(uint32_t { 0 })
      .UserFriendlyName("count")
      .StoreTo(&state.frame_count)
      .Build());
  return options;
}

} // namespace oxygen::examples::cli

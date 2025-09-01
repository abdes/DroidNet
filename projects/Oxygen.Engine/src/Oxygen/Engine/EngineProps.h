//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <vector>

#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::engine {

constexpr uint64_t kDefaultFixedUpdateDuration { 200'000 };
constexpr uint64_t kDefaultFixedIntervalDuration { 20'000 };

struct EngineProps {
  struct {
    std::string name;
    uint32_t version;
  } application;
  uint32_t target_fps { 0 }; //!< 0 = uncapped
  uint32_t frame_count { 0 }; //! 0 = unlimited
  Duration max_fixed_update_duration { kDefaultFixedUpdateDuration };

  bool enable_imgui_layer { true };
  platform::WindowIdType main_window_id {};

  std::vector<const char*> extensions {}; // Vulkan instance extensions
};

} // namespace oxygen::engine

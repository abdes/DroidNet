//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>

namespace oxygen::examples {

//! Bitmask of supported rendering features for pipeline discovery.
enum class PipelineFeature : std::uint32_t {
  kNone = 0,
  kOpaqueShading = OXYGEN_FLAG(0),
  kTransparentShading = OXYGEN_FLAG(1),
  kLightCulling = OXYGEN_FLAG(2),
  kPostProcess = OXYGEN_FLAG(3),
  kAll = 0xFFFFFFFFU,
};

OXYGEN_DEFINE_FLAGS_OPERATORS(PipelineFeature)

[[nodiscard]] inline auto to_string(PipelineFeature feature) -> std::string
{
  using Feature = PipelineFeature;

  if (feature == Feature::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  auto checked = Feature::kNone;

  auto check_and_append = [&](Feature flag, const char* name) {
    if ((feature & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  check_and_append(Feature::kOpaqueShading, "OpaqueShading");
  check_and_append(Feature::kTransparentShading, "TransparentShading");
  check_and_append(Feature::kLightCulling, "LightCulling");
  check_and_append(Feature::kPostProcess, "PostProcess");

  DCHECK_EQ_F(checked, feature, "Unchecked PipelineFeature value detected");

  if (result.empty()) {
    return "None";
  }
  return result;
}

} // namespace oxygen::examples

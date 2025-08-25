//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Core/api_export.h>

namespace oxygen {

struct ViewPort {
  float top_left_x { 0.f };
  float top_left_y { 0.f };
  float width { 0.f };
  float height { 0.f };
  float min_depth { 0.f };
  float max_depth { 1.f };

  [[nodiscard]] bool IsValid() const
  {
    return top_left_x >= 0.0f && top_left_y >= 0.0f && width > 0.0f
      && height > 0.0f && min_depth >= 0.0f && max_depth <= 1.0f
      && min_depth < max_depth;
  }
};

OXGN_CORE_NDAPI auto to_string(const ViewPort& viewport) -> std::string;

} // namespace oxygen

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
  float top_left_x { 0.F };
  float top_left_y { 0.F };
  float width { 0.F };
  float height { 0.F };
  float min_depth { 0.F };
  float max_depth { 1.F };

  [[nodiscard]] auto IsValid() const -> bool
  {
    return top_left_x >= 0.0F && top_left_y >= 0.0F && width > 0.0F
      && height > 0.0F && min_depth >= 0.0F && max_depth <= 1.0F
      && min_depth < max_depth;
  }
};

OXGN_CORE_NDAPI auto to_string(const ViewPort& viewport) -> std::string;

} // namespace oxygen

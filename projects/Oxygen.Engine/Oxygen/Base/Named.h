//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include "Oxygen/Base/Macros.h"

namespace oxygen {

class Named
{
 public:
  Named() = default;
  virtual ~Named() = default;

  OXYGEN_MAKE_NON_COPYABLE(Named)
  OXYGEN_DEFAULT_MOVABLE(Named)

  [[nodiscard]] virtual auto GetName() const noexcept -> std::string_view = 0;
  virtual void SetName(std::string_view name) noexcept = 0;
};

} // namespace oxygen

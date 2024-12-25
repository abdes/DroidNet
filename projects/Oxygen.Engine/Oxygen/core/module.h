//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/base/Time.h"
#include "oxygen/platform/Types.h"

namespace oxygen::core {

  class Module
  {
  public:
    Module() = default;
    virtual ~Module() = default;

    // Non-copyable
    Module(const Module&) = delete;
    auto operator=(const Module&)->Module & = delete;

    // Non-Movable
    Module(Module&& other) noexcept = delete;
    auto operator=(Module&& other) noexcept -> Module & = delete;

    virtual auto Initialize() -> void = 0;

    virtual auto ProcessInput(const platform::InputEvent& event) -> void = 0;
    virtual auto Update(Duration delta_time) -> void = 0;
    virtual auto FixedUpdate() -> void = 0;
    virtual auto Render() -> void = 0;

    virtual auto Shutdown() noexcept -> void = 0;
  };

}  // namespace oxygen::core

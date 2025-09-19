//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen {

namespace platform {
  class InputEvent;
  class InputSlot;
} // namespace platform

namespace input {
  class InputActionMapping;

  class InputMappingContext {
  public:
    OXGN_NPUT_API explicit InputMappingContext(std::string name);

    OXGN_NPUT_API void AddMapping(std::shared_ptr<InputActionMapping> mapping);

    [[nodiscard]] auto GetName() const { return name_; }

    OXGN_NPUT_API void HandleInput(
      const platform::InputSlot& slot, const platform::InputEvent& event) const;

    OXGN_NPUT_API [[nodiscard]] bool Update(
      oxygen::time::CanonicalDuration delta_time) const;

    // Flush staged input state from all mappings without producing action
    // edges. Use this when a higher-priority context consumed input to
    // prevent stale staged input from leaking into subsequent frames.
    OXGN_NPUT_API void FlushPending() const;

  private:
    std::string name_;

    std::vector<std::shared_ptr<InputActionMapping>> mappings_;
  };

} // namespace input
} // namespace oxygen

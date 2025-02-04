//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Oxygen/Base/TimeUtils.h"
#include "Oxygen/Input/api_export.h"

namespace oxygen {

namespace platform {
    class InputEvent;
    class InputSlot;
} // namespace platform

namespace input {
    class InputActionMapping;

    class InputMappingContext {
    public:
        OXYGEN_INPUT_API explicit InputMappingContext(std::string name);

        OXYGEN_INPUT_API void AddMapping(std::shared_ptr<InputActionMapping> mapping);

        [[nodiscard]] auto GetName() const { return name_; }

        OXYGEN_INPUT_API void HandleInput(const platform::InputSlot& slot,
            const platform::InputEvent& event) const;

        OXYGEN_INPUT_API [[nodiscard]] bool Update(Duration delta_time) const;

    private:
        std::string name_;

        std::vector<std::shared_ptr<InputActionMapping>> mappings_;
    };

} // namespace input
} // namespace oxygen

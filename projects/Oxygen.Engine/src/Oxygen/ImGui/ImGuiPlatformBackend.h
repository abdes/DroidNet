//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>

struct ImGuiContext;

namespace oxygen::imgui {

class ImGuiPlatformBackend : public Composition {
public:
    explicit ImGuiPlatformBackend(std::string_view name)
    {
        AddComponent<ObjectMetaData>(name);
    }

    [[nodiscard]] auto GetName() const noexcept -> std::string_view
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    virtual void NewFrame() = 0;
};

} // namespace oxygen

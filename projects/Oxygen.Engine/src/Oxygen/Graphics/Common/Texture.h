//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>

namespace oxygen::graphics {

class Texture : public Composition, public Named {
public:
    Texture()
        : Texture("Texture")
    {
    }

    explicit Texture(std::string_view name)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~Texture() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Texture);
    OXYGEN_DEFAULT_MOVABLE(Texture);


    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }
};

} // namespace oxygen::graphics

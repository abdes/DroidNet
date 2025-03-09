//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>

namespace oxygen::graphics {

/**
 * Buffer's view for binding
 */
struct BufferView {
    uint32_t firstElement = 0;
    uint32_t numElements = UINT32_MAX;
};

class Buffer : public Composition, public Named {
public:
    Buffer()
        : Buffer("Buffer")
    {
    }

    explicit Buffer(std::string_view name)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~Buffer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Buffer);
    OXYGEN_DEFAULT_MOVABLE(Buffer);

    virtual void Bind() = 0;
    virtual void* Map() = 0;
    virtual void Unmap() = 0;

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

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Composition.h"

namespace oxygen {

class ObjectMetaData final : public Component {
    OXYGEN_COMPONENT(ObjectMetaData)
public:
    explicit ObjectMetaData(const std::string_view name)
        : Component()
        , name_(name)
    {
    }

    ~ObjectMetaData() override = default;

    ObjectMetaData(const ObjectMetaData&) = default;
    ObjectMetaData& operator=(const ObjectMetaData&) = default;
    ObjectMetaData(ObjectMetaData&&) noexcept = default;
    ObjectMetaData& operator=(ObjectMetaData&&) noexcept = default;

    [[nodiscard]] constexpr std::string_view GetName() const noexcept { return name_; }
    constexpr void SetName(const std::string_view name) noexcept { name_ = name; }

    [[nodiscard]] auto IsCloneable() const noexcept -> bool override { return true; }
    [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
    {
        return std::make_unique<ObjectMetaData>(this->name_);
    }

private:
    std::string name_;
};

static_assert(IsComponent<ObjectMetaData>);

} // namespace oxygen

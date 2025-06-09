//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/Flags.h>

namespace oxygen::scene::detail {

class NodeData final : public Component {
    OXYGEN_COMPONENT(NodeData)
public:
    using Flags = SceneFlags<SceneNodeFlags>;
    static_assert(std::is_trivially_copyable_v<Flags>);
    Flags flags_;

    explicit NodeData(const Flags& flags)
        : flags_(flags)
    {
    }

    ~NodeData() override = default;

    OXYGEN_DEFAULT_COPYABLE(NodeData)

    //! Move constructor.
    /*!
     Although, classes in the class hierarchy may be well written, and do not
     touch the fields of the derived classes, we opt for a safer move that
     silences warnings with minimum risks and (theoretically) no performance
     impact. The lambda helper functions will safely move and rest the \p other
     fields, and ensure that it is left in a well-defined state. Compilers will
     optimize out the boilerplate and it won't make a difference in release
     builds, but the code is much more reliable this way.
    */
    NodeData(NodeData&& other) noexcept
        : Component(std::move(other))
        , flags_([](auto& src) {
            return std::exchange(src.flags_, kMovedFlags);
        }(other))
    {
    } // Move assignment
    auto operator=(NodeData&& other) noexcept -> NodeData&
    {
        if (this != &other) {
            flags_ = std::exchange(other.flags_, kMovedFlags);
            Component::operator=(std::move(other));
        }
        return *this;
    }

    [[nodiscard]] auto IsCloneable() const noexcept -> bool override
    {
        return true;
    }
    [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
    {
        return std::make_unique<NodeData>(this->flags_);
    }

private:
    static constexpr auto kMovedFlags = Flags {}.SetInheritedAll(false).SetFlag(
        SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));
};

} // namespace oxygen::scene::detail

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Core/Resources.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/TransformComponent.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class Scene;

class SceneNodeData final : public Component {
    OXYGEN_COMPONENT(SceneNodeData)
public:
    using Flags = SceneFlags<SceneNodeFlags>;
    OXYGEN_SCENE_API explicit SceneNodeData(Flags flags);
    OXYGEN_SCENE_API ~SceneNodeData() override = default;
    OXYGEN_DEFAULT_COPYABLE(SceneNodeData)
    OXYGEN_DEFAULT_MOVABLE(SceneNodeData)
    [[nodiscard]] auto GetFlags() const noexcept -> const Flags& { return flags_; }
    [[nodiscard]] auto GetFlags() noexcept -> Flags& { return flags_; }

private:
    Flags flags_;
};

class SceneNodeImpl : public Composition {
    OXYGEN_TYPED(SceneNodeImpl)
public:
    using Flags = SceneNodeData::Flags;

private:
    static constexpr auto kDefaultFlags
        = Flags {}
              .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true))
              .SetFlag(SceneNodeFlags::kCastsShadows, SceneFlag {}.SetInheritedBit(true))
              .SetFlag(SceneNodeFlags::kReceivesShadows, SceneFlag {}.SetInheritedBit(true))
              .SetFlag(SceneNodeFlags::kRayCastingSelectable, SceneFlag {}.SetInheritedBit(true));

public:
    OXYGEN_SCENE_API explicit SceneNodeImpl(const std::string& name, Flags flags = kDefaultFlags);
    [[nodiscard]] OXYGEN_SCENE_API auto GetName() const noexcept -> std::string_view;
    OXYGEN_SCENE_API void SetName(std::string_view name) noexcept;
    [[nodiscard]] OXYGEN_SCENE_API auto GetFlags() const noexcept -> const Flags&;
    [[nodiscard]] OXYGEN_SCENE_API auto GetFlags() noexcept -> Flags&;
    [[nodiscard]] OXYGEN_SCENE_API auto GetParent() const noexcept -> ResourceHandle;
    [[nodiscard]] OXYGEN_SCENE_API auto GetFirstChild() const noexcept -> ResourceHandle;
    [[nodiscard]] OXYGEN_SCENE_API auto GetNextSibling() const noexcept -> ResourceHandle;
    [[nodiscard]] OXYGEN_SCENE_API auto GetPrevSibling() const noexcept -> ResourceHandle;
    OXYGEN_SCENE_API void SetParent(ResourceHandle parent) noexcept;
    OXYGEN_SCENE_API void SetFirstChild(ResourceHandle child) noexcept;
    OXYGEN_SCENE_API void SetNextSibling(ResourceHandle sibling) noexcept;
    OXYGEN_SCENE_API void SetPrevSibling(ResourceHandle sibling) noexcept;
    OXYGEN_SCENE_API void MarkTransformDirty() noexcept;
    OXYGEN_SCENE_API void ClearTransformDirty() noexcept;
    [[nodiscard]] OXYGEN_SCENE_API auto IsTransformDirty() const noexcept -> bool;
    OXYGEN_SCENE_API void UpdateTransforms(const Scene& scene);

private:
    [[nodiscard]] constexpr auto ShouldIgnoreParentTransform() const
    {
        return GetFlags().GetEffectiveValue(SceneNodeFlags::kIgnoreParentTransform);
    }
    ResourceHandle parent_;
    ResourceHandle first_child_;
    ResourceHandle next_sibling_;
    ResourceHandle prev_sibling_;
    bool transform_dirty_ = true;
};

} // namespace oxygen::scene

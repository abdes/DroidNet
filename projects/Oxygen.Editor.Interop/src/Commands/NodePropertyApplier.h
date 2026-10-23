//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/Flags.h>

namespace oxygen::interop::module {

  //! Maps an authored node field onto its native scene flag.
  [[nodiscard]] inline auto ToSceneNodeFlag(const NodeField field) noexcept
    -> std::optional<scene::SceneNodeFlags>
  {
    switch (field) {
    case NodeField::kVisible:
      return scene::SceneNodeFlags::kVisible;
    case NodeField::kCastsShadows:
      return scene::SceneNodeFlags::kCastsShadows;
    case NodeField::kReceivesShadows:
      return scene::SceneNodeFlags::kReceivesShadows;
    case NodeField::kStatic:
      return scene::SceneNodeFlags::kStatic;
    case NodeField::kRayCastingSelectable:
      return scene::SceneNodeFlags::kRayCastingSelectable;
    case NodeField::kIgnoreParentTransform:
      return scene::SceneNodeFlags::kIgnoreParentTransform;
    case NodeField::kCount:
      break;
    }
    return std::nullopt;
  }

  //! Wire values of a node flag with a source mode, numbered as the editor's
  //! `VisibilitySource` and `ShadowSource`.
  enum class NodeFlagSource : std::uint8_t {
    kInherit = 0,
    kLocalOn = 1,
    kLocalOff = 2,
  };

  //! Whether an authored node field carries a source mode rather than a
  //! local boolean.
  [[nodiscard]] constexpr auto HasSourceMode(const NodeField field) noexcept
    -> bool
  {
    return field == NodeField::kVisible || field == NodeField::kCastsShadows
      || field == NodeField::kReceivesShadows;
  }

  //! Applies authored node flags. Visibility and the geometry shadow flags
  //! take a source mode, an int32 field: Inherit, or a local value. The other
  //! flags take a local bool.
  class NodePropertyApplier final : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept -> ComponentId override
    {
      return ComponentId::kNode;
    }

    void Apply(scene::SceneNode& node,
      std::span<const PropertyEntry> entries) override
    {
      for (const auto& entry : entries) {
        if (!IsValid(entry)) {
          throw std::invalid_argument(
            std::to_string(entry.field) + ": invalid node flag property");
        }
      }

      auto flags = node.GetFlags();
      if (!flags) {
        throw std::invalid_argument("The scene node has no flags");
      }
      for (const auto& entry : entries) {
        const auto field = static_cast<NodeField>(entry.field);
        const auto flag = *ToSceneNodeFlag(field);
        if (!HasSourceMode(field)) {
          flags->get().SetLocalValue(flag, entry.Bool());
        } else if (const auto source = static_cast<NodeFlagSource>(entry.Int32());
          source == NodeFlagSource::kInherit) {
          flags->get().SetInherited(flag, true);
        } else {
          flags->get().SetLocalValue(flag, source == NodeFlagSource::kLocalOn);
        }
      }
    }

  private:
    [[nodiscard]] static auto IsValid(const PropertyEntry& entry) noexcept
      -> bool
    {
      const auto field = static_cast<NodeField>(entry.field);
      if (!ToSceneNodeFlag(field)) {
        return false;
      }
      if (!HasSourceMode(field)) {
        return true;
      }
      const auto source = entry.Int32();
      return source >= static_cast<std::int32_t>(NodeFlagSource::kInherit)
        && source <= static_cast<std::int32_t>(NodeFlagSource::kLocalOff);
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

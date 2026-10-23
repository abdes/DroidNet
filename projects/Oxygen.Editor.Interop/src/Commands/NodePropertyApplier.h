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
#include <Commands/SchemaEnums.h>
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

  //! Whether an authored node field carries a source mode (its schema value
  //! type is an int32 enumeration) rather than a local boolean.
  [[nodiscard]] constexpr auto HasSourceMode(const NodeField field) noexcept
    -> bool
  {
    return FindFieldType(ComponentId::kNode, static_cast<std::uint16_t>(field))
      == FieldType::kInt32;
  }

  //! The local value a visibility source sets, or nullopt to inherit.
  [[nodiscard]] constexpr auto LocalValueOf(const VisibilitySource source)
    noexcept -> std::optional<bool>
  {
    switch (source) {
    case VisibilitySource::kInherit:
      return std::nullopt;
    case VisibilitySource::kShown:
      return true;
    case VisibilitySource::kHidden:
      return false;
    }
    return std::nullopt;
  }

  //! The local value a shadow source sets, or nullopt to inherit.
  [[nodiscard]] constexpr auto LocalValueOf(const ShadowSource source) noexcept
    -> std::optional<bool>
  {
    switch (source) {
    case ShadowSource::kInherit:
      return std::nullopt;
    case ShadowSource::kOn:
      return true;
    case ShadowSource::kOff:
      return false;
    }
    return std::nullopt;
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
        } else if (const auto local = SourceLocalValue(field, entry.Int32())) {
          flags->get().SetLocalValue(flag, *local);
        } else {
          flags->get().SetInherited(flag, true);
        }
      }
    }

  private:
    //! The local value a validated source-mode field sets, or nullopt to
    //! inherit. Visibility takes a `VisibilitySource`, the shadow flags a
    //! `ShadowSource`.
    [[nodiscard]] static auto SourceLocalValue(
      const NodeField field, const std::int32_t wire) -> std::optional<bool>
    {
      switch (field) {
      case NodeField::kVisible:
        return LocalValueOf(ValidatedMember(ToVisibilitySource(wire)));
      case NodeField::kCastsShadows:
      case NodeField::kReceivesShadows:
        return LocalValueOf(ValidatedMember(ToShadowSource(wire)));
      default:
        break;
      }
      throw std::invalid_argument(
        std::to_string(static_cast<std::uint16_t>(field))
        + ": node field has no source mode");
    }

    [[nodiscard]] static auto IsValid(const PropertyEntry& entry)
      -> bool
    {
      const auto field = static_cast<NodeField>(entry.field);
      return ToSceneNodeFlag(field).has_value() && IsInRange(entry);
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

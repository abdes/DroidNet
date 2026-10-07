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

  enum class NodeField : std::uint16_t {
    kVisible = 0,
    kCastsShadows = 1,
    kReceivesShadows = 2,
    kCount,
  };

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
    case NodeField::kCount:
      break;
    }
    return std::nullopt;
  }

  //! Applies authored rendering flags as local values, overriding inheritance.
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
        if (!ToSceneNodeFlag(static_cast<NodeField>(entry.field))
          || (entry.value != 0.0F && entry.value != 1.0F)) {
          throw std::invalid_argument(
            std::to_string(entry.field) + ": invalid node flag property");
        }
      }

      auto flags = node.GetFlags();
      if (!flags) {
        throw std::invalid_argument("The scene node has no flags");
      }
      for (const auto& entry : entries) {
        flags->get().SetLocalValue(
          *ToSceneNodeFlag(static_cast<NodeField>(entry.field)),
          entry.value != 0.0F);
      }
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  //! Component-local field ids for `TransformComponent` properties.
  /*!
   These ids are *local* to the Transform applier; they are never
   exposed on the wire alone — they always travel paired with
   `ComponentId::kTransform`. Adding a field is one new enumerator
   plus one row in `TransformPropertyApplier::kFieldTable`.
  */
  enum class TransformField : std::uint16_t {
    kPositionX = 0,
    kPositionY = 1,
    kPositionZ = 2,
    kRotationXDegrees = 3,
    kRotationYDegrees = 4,
    kRotationZDegrees = 5,
    kScaleX = 6,
    kScaleY = 7,
    kScaleZ = 8,
    kCount,
  };

  //! Applies §5.3 property entries to `oxygen::scene::TransformComponent`.
  /*!
   Implementation note: all transform sub-fields read once, mutate a
   local working state, and write back exactly once per dirty channel.
   The dispatch table is a `std::array` indexed by `TransformField` —
   adding a property is one row.
  */
  class TransformPropertyApplier final : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept
      -> ComponentId override
    {
      return ComponentId::kTransform;
    }

    void Apply(oxygen::scene::SceneNode& node,
      std::span<const PropertyEntry> entries) override
    {
      auto transform = node.GetTransform();

      // Local working state — read-modify-write.
      State state {};
      if (auto current = transform.GetLocalPosition()) {
        state.position = *current;
      }
      if (auto current = transform.GetLocalRotation()) {
        state.euler_degrees
          = glm::degrees(glm::eulerAngles(*current));
      }
      if (auto current = transform.GetLocalScale()) {
        state.scale = *current;
      }

      for (const auto& entry : entries) {
        if (!std::isfinite(entry.value)) {
          LOG_F(WARNING,
            "TransformPropertyApplier skipped field {}: non-finite value {}",
            entry.field, entry.value);
          continue;
        }

        const auto idx = static_cast<std::size_t>(entry.field);
        if (idx >= static_cast<std::size_t>(TransformField::kCount)) {
          LOG_F(WARNING,
            "TransformPropertyApplier skipped unknown field id {}",
            entry.field);
          continue;
        }
        kFieldTable[idx](state, entry.value);
      }

      if (state.position_dirty) {
        transform.SetLocalPosition(state.position);
      }
      if (state.rotation_dirty) {
        const glm::quat rot(glm::radians(state.euler_degrees));
        transform.SetLocalRotation(rot);
      }
      if (state.scale_dirty) {
        transform.SetLocalScale(state.scale);
      }
    }

  private:
    struct State {
      glm::vec3 position { 0.0F };
      glm::vec3 euler_degrees { 0.0F };
      glm::vec3 scale { 1.0F };
      bool position_dirty { false };
      bool rotation_dirty { false };
      bool scale_dirty { false };
    };

    using Setter = void (*)(State&, float);

    static void SetPx(State& s, float v) { s.position.x = v; s.position_dirty = true; }
    static void SetPy(State& s, float v) { s.position.y = v; s.position_dirty = true; }
    static void SetPz(State& s, float v) { s.position.z = v; s.position_dirty = true; }
    static void SetRx(State& s, float v) { s.euler_degrees.x = v; s.rotation_dirty = true; }
    static void SetRy(State& s, float v) { s.euler_degrees.y = v; s.rotation_dirty = true; }
    static void SetRz(State& s, float v) { s.euler_degrees.z = v; s.rotation_dirty = true; }
    static void SetSx(State& s, float v) { s.scale.x = v; s.scale_dirty = true; }
    static void SetSy(State& s, float v) { s.scale.y = v; s.scale_dirty = true; }
    static void SetSz(State& s, float v) { s.scale.z = v; s.scale_dirty = true; }

    //! Component-local field dispatch table. Size == kCount.
    static constexpr std::array<Setter,
      static_cast<std::size_t>(TransformField::kCount)>
      kFieldTable {
        &TransformPropertyApplier::SetPx, &TransformPropertyApplier::SetPy,
        &TransformPropertyApplier::SetPz, &TransformPropertyApplier::SetRx,
        &TransformPropertyApplier::SetRy, &TransformPropertyApplier::SetRz,
        &TransformPropertyApplier::SetSx, &TransformPropertyApplier::SetSy,
        &TransformPropertyApplier::SetSz,
      };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

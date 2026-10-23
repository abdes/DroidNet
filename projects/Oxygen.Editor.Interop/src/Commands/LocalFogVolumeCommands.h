//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>

#include <Commands/PropertyKeys.h>
#include <EditorModule/EditorCommand.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  //! The authored values of a local fog volume, as the editor sends them.
  //! A value left out is its field's schema default.
  struct LocalFogVolumeValues {
    bool enabled { data::schema::local_fog_volume::kEnabledDefault };
    float radial_fog_extinction {
      data::schema::local_fog_volume::kRadialFogExtinctionDefault,
    };
    float height_fog_extinction {
      data::schema::local_fog_volume::kHeightFogExtinctionDefault,
    };
    float height_fog_falloff {
      data::schema::local_fog_volume::kHeightFogFalloffDefault,
    };
    float height_fog_offset {
      data::schema::local_fog_volume::kHeightFogOffsetDefault,
    };
    float fog_phase_g { data::schema::local_fog_volume::kFogPhaseGDefault };
    Vec3 fog_albedo {
      data::schema::local_fog_volume::kFogAlbedoRDefault,
      data::schema::local_fog_volume::kFogAlbedoGDefault,
      data::schema::local_fog_volume::kFogAlbedoBDefault,
    };
    Vec3 fog_emissive {
      data::schema::local_fog_volume::kFogEmissiveRDefault,
      data::schema::local_fog_volume::kFogEmissiveGDefault,
      data::schema::local_fog_volume::kFogEmissiveBDefault,
    };
    std::int32_t sort_priority {
      data::schema::local_fog_volume::kSortPriorityDefault,
    };
  };

  //! Builds a local fog volume from authored values.
  /*!
   The volume's setters clamp, so each value is checked against its field
   (`IsInRange`) before it is set.

   \throws std::invalid_argument when a value is outside its field's range.
  */
  [[nodiscard]] inline auto MakeLocalFogVolume(const LocalFogVolumeValues& values)
    -> scene::environment::LocalFogVolume
  {
    const auto check
      = [](const LocalFogVolumeField field, const FieldValue value) {
          RequireInRange(ComponentId::kLocalFogVolume, field, value);
        };
    check(LocalFogVolumeField::kEnabled, values.enabled);
    check(LocalFogVolumeField::kRadialFogExtinction,
      values.radial_fog_extinction);
    check(LocalFogVolumeField::kHeightFogExtinction,
      values.height_fog_extinction);
    check(LocalFogVolumeField::kHeightFogFalloff, values.height_fog_falloff);
    check(LocalFogVolumeField::kHeightFogOffset, values.height_fog_offset);
    check(LocalFogVolumeField::kFogPhaseG, values.fog_phase_g);
    check(LocalFogVolumeField::kFogAlbedoR, values.fog_albedo.x);
    check(LocalFogVolumeField::kFogAlbedoG, values.fog_albedo.y);
    check(LocalFogVolumeField::kFogAlbedoB, values.fog_albedo.z);
    check(LocalFogVolumeField::kFogEmissiveR, values.fog_emissive.x);
    check(LocalFogVolumeField::kFogEmissiveG, values.fog_emissive.y);
    check(LocalFogVolumeField::kFogEmissiveB, values.fog_emissive.z);
    check(LocalFogVolumeField::kSortPriority, values.sort_priority);

    scene::environment::LocalFogVolume volume;
    volume.SetEnabled(values.enabled);
    volume.SetRadialFogExtinction(values.radial_fog_extinction);
    volume.SetHeightFogExtinction(values.height_fog_extinction);
    volume.SetHeightFogFalloff(values.height_fog_falloff);
    volume.SetHeightFogOffset(values.height_fog_offset);
    volume.SetFogPhaseG(values.fog_phase_g);
    volume.SetFogAlbedo(values.fog_albedo);
    volume.SetFogEmissive(values.fog_emissive);
    volume.SetSortPriority(values.sort_priority);
    return volume;
  }

  //! Attaches or replaces the local fog volume on one node.
  class AttachLocalFogVolumeCommand final : public EditorCommand {
  public:
    AttachLocalFogVolumeCommand(
      scene::NodeHandle node, scene::environment::LocalFogVolume volume)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
      , volume_(volume)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) {
        return;
      }
      auto impl = node->GetImpl();
      if (!impl) {
        return;
      }
      using scene::environment::LocalFogVolume;
      if (impl->get().HasComponent<LocalFogVolume>()) {
        impl->get().ReplaceComponent<LocalFogVolume>(volume_);
      } else {
        impl->get().AddComponent<LocalFogVolume>(volume_);
      }
      context.Scene->Update(false);
    }

  private:
    scene::NodeHandle node_;
    scene::environment::LocalFogVolume volume_;
  };

  //! Removes the local fog volume from one node, if it has one.
  class DetachLocalFogVolumeCommand final : public EditorCommand {
  public:
    explicit DetachLocalFogVolumeCommand(scene::NodeHandle node)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) {
        return;
      }
      auto impl = node->GetImpl();
      using scene::environment::LocalFogVolume;
      if (impl && impl->get().HasComponent<LocalFogVolume>()) {
        impl->get().RemoveComponent<LocalFogVolume>();
        context.Scene->Update(false);
      }
    }

  private:
    scene::NodeHandle node_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

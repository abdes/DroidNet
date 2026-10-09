//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  //! Field ids of the local fog volume component.
  enum class LocalFogVolumeField : std::uint16_t {
    kEnabled = 0,
    kRadialFogExtinction = 1,
    kHeightFogExtinction = 2,
    kHeightFogFalloff = 3,
    kHeightFogOffset = 4,
    kFogPhaseG = 5,
    kFogAlbedoR = 6,
    kFogAlbedoG = 7,
    kFogAlbedoB = 8,
    kFogEmissiveR = 9,
    kFogEmissiveG = 10,
    kFogEmissiveB = 11,
    kSortPriority = 12,
    kCount = 13,
  };

  //! Applies one complete local fog volume candidate; an invalid entry rejects
  //! the whole edit and leaves the component unchanged.
  class LocalFogVolumePropertyApplier final : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept -> ComponentId override
    {
      return ComponentId::kLocalFogVolume;
    }

    void Apply(scene::SceneNode& node,
      std::span<const PropertyEntry> entries) override
    {
      using scene::environment::LocalFogVolume;
      auto impl = node.GetImpl();
      if (!impl || !impl->get().HasComponent<LocalFogVolume>()) {
        throw std::invalid_argument("Node has no local fog volume");
      }
      auto& volume = impl->get().GetComponent<LocalFogVolume>();
      auto candidate = volume;
      for (const auto& entry : entries) {
        if (!ApplyEntry(candidate, entry)) {
          throw std::invalid_argument(std::to_string(entry.field)
            + ": Invalid local fog volume property encoding");
        }
      }
      volume = candidate;
    }

  private:
    static auto ApplyEntry(scene::environment::LocalFogVolume& volume,
      const PropertyEntry& entry) -> bool
    {
      const float value = entry.value;
      if (!std::isfinite(value)) {
        return false;
      }
      auto albedo = volume.GetFogAlbedo();
      auto emissive = volume.GetFogEmissive();
      switch (static_cast<LocalFogVolumeField>(entry.field)) {
      case LocalFogVolumeField::kEnabled:
        volume.SetEnabled(value != 0.0F);
        return value == 0.0F || value == 1.0F;
      case LocalFogVolumeField::kRadialFogExtinction:
        volume.SetRadialFogExtinction(value);
        return value >= 0.0F;
      case LocalFogVolumeField::kHeightFogExtinction:
        volume.SetHeightFogExtinction(value);
        return value >= 0.0F;
      case LocalFogVolumeField::kHeightFogFalloff:
        volume.SetHeightFogFalloff(value);
        return value >= 0.0F;
      case LocalFogVolumeField::kHeightFogOffset:
        volume.SetHeightFogOffset(value);
        return true;
      case LocalFogVolumeField::kFogPhaseG:
        volume.SetFogPhaseG(value);
        return value >= 0.0F && value < 1.0F;
      case LocalFogVolumeField::kFogAlbedoR:
        albedo.x = value;
        volume.SetFogAlbedo(albedo);
        return value >= 0.0F && value <= 1.0F;
      case LocalFogVolumeField::kFogAlbedoG:
        albedo.y = value;
        volume.SetFogAlbedo(albedo);
        return value >= 0.0F && value <= 1.0F;
      case LocalFogVolumeField::kFogAlbedoB:
        albedo.z = value;
        volume.SetFogAlbedo(albedo);
        return value >= 0.0F && value <= 1.0F;
      case LocalFogVolumeField::kFogEmissiveR:
        emissive.x = value;
        volume.SetFogEmissive(emissive);
        return value >= 0.0F;
      case LocalFogVolumeField::kFogEmissiveG:
        emissive.y = value;
        volume.SetFogEmissive(emissive);
        return value >= 0.0F;
      case LocalFogVolumeField::kFogEmissiveB:
        emissive.z = value;
        volume.SetFogEmissive(emissive);
        return value >= 0.0F;
      case LocalFogVolumeField::kSortPriority:
        volume.SetSortPriority(static_cast<int>(value));
        return std::floor(value) == value && value >= -127.0F
          && value <= 127.0F;
      default:
        return false;
      }
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

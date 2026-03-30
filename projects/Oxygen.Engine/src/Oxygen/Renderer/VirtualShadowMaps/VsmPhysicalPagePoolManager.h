//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

class VsmPhysicalPagePoolManager {
public:
  OXGN_RNDR_API explicit VsmPhysicalPagePoolManager(Graphics* gfx) noexcept;

  OXYGEN_MAKE_NON_COPYABLE(VsmPhysicalPagePoolManager)
  OXYGEN_MAKE_NON_MOVABLE(VsmPhysicalPagePoolManager)

  OXGN_RNDR_API ~VsmPhysicalPagePoolManager();

  OXGN_RNDR_API auto EnsureShadowPool(const VsmPhysicalPoolConfig& config)
    -> VsmPhysicalPoolChangeResult;
  OXGN_RNDR_API auto EnsureHzbPool(const VsmHzbPoolConfig& config)
    -> VsmHzbPoolChangeResult;
  OXGN_RNDR_API auto Reset() -> void;

  OXGN_RNDR_NDAPI auto IsShadowPoolAvailable() const noexcept -> bool;
  OXGN_RNDR_NDAPI auto IsHzbPoolAvailable() const noexcept -> bool;
  OXGN_RNDR_NDAPI auto GetPoolIdentity() const noexcept -> std::uint64_t;
  OXGN_RNDR_NDAPI auto GetSliceCount() const noexcept -> std::uint32_t;
  OXGN_RNDR_NDAPI auto GetSliceRoles() const noexcept
    -> std::span<const VsmPhysicalPoolSliceRole>;
  OXGN_RNDR_NDAPI auto GetTileCapacity() const noexcept -> std::uint32_t;
  OXGN_RNDR_NDAPI auto GetTilesPerAxis() const noexcept -> std::uint32_t;

  // Returns a per-frame snapshot carrying shared_ptr handles to GPU resources.
  // Callers must not hold the returned snapshot across pool resets.
  OXGN_RNDR_NDAPI auto GetShadowPoolSnapshot() const noexcept
    -> VsmPhysicalPoolSnapshot;
  // Returns a per-frame snapshot carrying shared_ptr handles to GPU resources.
  // Callers must not hold the returned snapshot across pool resets.
  OXGN_RNDR_NDAPI auto GetHzbPoolSnapshot() const noexcept
    -> VsmHzbPoolSnapshot;
  OXGN_RNDR_NDAPI auto GetMetadataBuffer() const noexcept
    -> std::shared_ptr<const graphics::Buffer>;

  OXGN_RNDR_NDAPI auto IsCompatible(
    const VsmPhysicalPoolConfig& config) const noexcept -> bool;
  OXGN_RNDR_NDAPI auto ComputeCompatibility(
    const VsmPhysicalPoolConfig& config) const noexcept
    -> VsmPhysicalPoolCompatibilityResult;

private:
  struct VsmPhysicalPoolRuntimeState {
    std::uint64_t pool_identity { 0 };
    VsmPhysicalPoolConfig config {};
    bool is_available { false };
    std::uint32_t tiles_per_axis { 0 };
  };

  struct VsmHzbPoolRuntimeState {
    VsmHzbPoolConfig config {};
    bool is_available { false };
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    std::uint32_t array_size { 0 };
  };

  struct VsmPhysicalPoolResources {
    std::shared_ptr<graphics::Texture> shadow_texture {};
    std::shared_ptr<graphics::Buffer> metadata_buffer {};
  };

  struct VsmHzbPoolResources {
    std::shared_ptr<graphics::Texture> texture {};
  };

  Graphics* gfx_ { nullptr };
  std::uint64_t next_pool_identity_ { 1 };
  VsmPhysicalPoolRuntimeState shadow_state_ {};
  VsmHzbPoolRuntimeState hzb_state_ {};
  VsmPhysicalPoolResources shadow_resources_ {};
  VsmHzbPoolResources hzb_resources_ {};
};

} // namespace oxygen::renderer::vsm

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

enum class SceneTextureQueueAffinity : std::uint8_t {
  kGraphicsOnly = 0,
  kAsyncCompute = 1,
};

struct SceneTextureLeaseKey {
  glm::uvec2 extent { 0, 0 };
  std::uint32_t render_scale_milli { 1000U };
  Format scene_color_format { Format::kRGBA16Float };
  Format scene_depth_stencil_format { Format::kDepth32Stencil8 };
  Format partial_depth_format { Format::kR32Float };
  std::uint32_t gbuffer_count { 4U };
  bool enable_velocity { true };
  bool enable_custom_depth { false };
  std::uint32_t msaa_sample_count { 1U };
  std::uint32_t editor_primitive_sample_count { 1U };
  bool require_editor_primitive_attachments { false };
  bool reverse_z { true };
  bool hdr_output { true };
  bool require_debug_attachment { false };
  SceneTextureQueueAffinity queue_affinity {
    SceneTextureQueueAffinity::kGraphicsOnly
  };

  [[nodiscard]] OXGN_VRTX_API static auto FromConfig(
    const SceneTexturesConfig& config) -> SceneTextureLeaseKey;

  [[nodiscard]] friend auto operator==(const SceneTextureLeaseKey& lhs,
    const SceneTextureLeaseKey& rhs) noexcept -> bool = default;
};

class SceneTextureLeasePool;

class SceneTextureLease {
public:
  OXGN_VRTX_API SceneTextureLease() noexcept = default;
  OXGN_VRTX_API ~SceneTextureLease();

  SceneTextureLease(const SceneTextureLease&) = delete;
  auto operator=(const SceneTextureLease&) -> SceneTextureLease& = delete;
  OXGN_VRTX_API SceneTextureLease(SceneTextureLease&& other) noexcept;
  OXGN_VRTX_API auto operator=(SceneTextureLease&& other) noexcept
    -> SceneTextureLease&;

  [[nodiscard]] OXGN_VRTX_API auto IsValid() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneTextures() -> SceneTextures&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneTextures() const
    -> const SceneTextures&;
  [[nodiscard]] OXGN_VRTX_API auto GetKey() const -> const SceneTextureLeaseKey&;
  [[nodiscard]] OXGN_VRTX_API auto GetLeaseId() const noexcept -> std::uint64_t;
  OXGN_VRTX_API void Release() noexcept;

private:
  friend class SceneTextureLeasePool;

  SceneTextureLease(SceneTextureLeasePool& pool, std::size_t slot,
    std::uint64_t generation) noexcept;

  SceneTextureLeasePool* pool_ { nullptr };
  std::size_t slot_ { 0U };
  std::uint64_t generation_ { 0U };
};

class SceneTextureLeasePool {
public:
  OXGN_VRTX_API explicit SceneTextureLeasePool(Graphics& gfx,
    SceneTexturesConfig base_config, std::size_t max_live_leases_per_key = 16U);
  OXGN_VRTX_API ~SceneTextureLeasePool();

  SceneTextureLeasePool(const SceneTextureLeasePool&) = delete;
  auto operator=(const SceneTextureLeasePool&) -> SceneTextureLeasePool& = delete;
  SceneTextureLeasePool(SceneTextureLeasePool&&) = delete;
  auto operator=(SceneTextureLeasePool&&) -> SceneTextureLeasePool& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Acquire(const SceneTextureLeaseKey& key)
    -> SceneTextureLease;
  [[nodiscard]] OXGN_VRTX_API auto GetAllocationCount() const noexcept
    -> std::size_t;
  [[nodiscard]] OXGN_VRTX_API auto GetLiveLeaseCount() const noexcept
    -> std::size_t;
  [[nodiscard]] OXGN_VRTX_API auto GetLeaseCountForKey(
    const SceneTextureLeaseKey& key) const noexcept -> std::size_t;
  [[nodiscard]] OXGN_VRTX_API auto GetMaxLiveLeasesPerKey() const noexcept
    -> std::size_t;

private:
  friend class SceneTextureLease;

  struct Entry {
    SceneTextureLeaseKey key {};
    std::unique_ptr<SceneTextures> scene_textures;
    bool active { false };
    std::uint64_t generation { 0U };
    std::uint64_t lease_id { 0U };
  };

  OXGN_VRTX_API void Release(
    std::size_t slot, std::uint64_t generation) noexcept;
  [[nodiscard]] OXGN_VRTX_API auto BuildConfig(
    const SceneTextureLeaseKey& key) const -> SceneTexturesConfig;
  [[nodiscard]] OXGN_VRTX_API auto CountLiveLeasesForKey(
    const SceneTextureLeaseKey& key) const noexcept -> std::size_t;

  Graphics& gfx_;
  SceneTexturesConfig base_config_;
  std::size_t max_live_leases_per_key_ { 16U };
  std::vector<Entry> entries_;
  std::size_t allocation_count_ { 0U };
  std::uint64_t next_lease_id_ { 1U };
};

} // namespace oxygen::vortex

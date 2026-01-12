//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>

namespace oxygen::engine::internal {
class IblManager;
}

namespace oxygen::engine {

//! Computes diffuse irradiance and specular prefilter cubemaps for IBL.
/*!
 Runs compute filtering on the current environment cubemap source (SkyLight or
 fallback) and writes results into textures owned by `internal::IblManager`.

 This pass follows the engine's bindless ABI:
 - Scene constants are bound as a root CBV at `b1`.
 - Root constants are bound at `b2` with `{g_DrawIndex, g_PassConstantsIndex}`.
 - Pass constants are stored as a bindless structured buffer SRV indexed by
 `g_PassConstantsIndex`, and selected per dispatch via `g_DrawIndex`.
*/
class IblComputePass final : public RenderPass {
public:
  explicit IblComputePass(std::string name);
  ~IblComputePass() override;

  auto ValidateConfig() -> void override { }
  auto DoPrepareResources([[maybe_unused]] graphics::CommandRecorder& recorder)
    -> co::Co<> override
  {
    co_return;
  }
  auto OnPrepareResources([[maybe_unused]] graphics::CommandRecorder& recorder)
    -> void override
  {
  }
  auto OnExecute([[maybe_unused]] graphics::CommandRecorder& recorder)
    -> void override
  {
  }
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;

  auto SetSourceCubemapSlot(const ShaderVisibleIndex slot) -> void
  {
    explicit_source_slot_ = slot;
  }

  //! Request an IBL regeneration on the next frame.
  /*!
   If the IBL manager is not dirty, this forces a regeneration anyway.
  */
  auto RequestRegenerationOnce() noexcept -> void;

private:
  //! Must match HLSL `IblFilteringPassConstants` in Renderer/IblFiltering.hlsl.
  struct alignas(16) IblFilteringPassConstants {
    uint32_t source_cubemap_slot { kInvalidDescriptorSlot };
    uint32_t target_uav_slot { kInvalidDescriptorSlot };
    float roughness { 0.0F };
    uint32_t face_size { 0 };
  };
  static_assert(sizeof(IblFilteringPassConstants) == 16,
    "IblFilteringPassConstants must be 16 bytes");

  // TODO: Move this to a shared config or dynamic resizing buffer strategy.
  static constexpr uint32_t kMaxDispatches = 16U;

  auto EnsurePassConstantsBuffer() -> void;
  auto EnsurePipelineStateDescs() -> void;

  auto ResolveSourceCubemapSlot() const noexcept -> ShaderVisibleIndex;

  auto DispatchIrradiance(graphics::CommandRecorder& recorder,
    internal::IblManager& ibl, ShaderVisibleIndex source_slot) -> void;
  auto DispatchPrefilter(graphics::CommandRecorder& recorder,
    internal::IblManager& ibl, ShaderVisibleIndex source_slot) -> void;

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  void* pass_constants_mapped_ { nullptr };
  ShaderVisibleIndex pass_constants_srv_index_ { kInvalidShaderVisibleIndex };

  std::optional<graphics::ComputePipelineDesc> irradiance_pso_desc_;
  std::optional<graphics::ComputePipelineDesc> prefilter_pso_desc_;

  ShaderVisibleIndex explicit_source_slot_ { kInvalidShaderVisibleIndex };

  // Tracks the expected D3D12 resource state across frames so each command
  // recorder can begin tracking with the correct initial state.
  bool irradiance_in_shader_resource_state_ { false };
  bool prefilter_in_shader_resource_state_ { false };

  // One-shot diagnostics to avoid spamming logs on every frame.
  bool logged_missing_env_manager_ { false };
  bool logged_missing_ibl_manager_ { false };
  bool logged_missing_source_slot_ { false };

  std::atomic<bool> regeneration_requested_ { false };
};

} // namespace oxygen::engine

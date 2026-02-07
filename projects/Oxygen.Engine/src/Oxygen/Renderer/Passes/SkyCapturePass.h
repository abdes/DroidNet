//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Internal/ISkyCaptureProvider.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class Texture;
class Buffer;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::engine {

//! Configuration for the sky capture pass.
struct SkyCapturePassConfig {
  //! Resolution for each cubemap face (e.g., 128x128).
  uint32_t resolution { 128u };

  //! Debug name for diagnostics.
  std::string debug_name { "SkyCapturePass" };
};

//! WIP: captures the scene sky into a cubemap for sky lighting.
/*!
 The SkyCapturePass renders the current sky background (Atmosphere or Sphere)
 into an internal cubemap. This captured cubemap is then used to drive the
 IBL filtering pipeline (irradiance and prefilter maps).

 The pass only executes when the sky content has changed.
*/
class SkyCapturePass final : public GraphicsRenderPass,
                             public internal::ISkyCaptureProvider {
public:
  using Config = SkyCapturePassConfig;

  explicit SkyCapturePass(
    observer_ptr<Graphics> gfx, std::shared_ptr<SkyCapturePassConfig> config);
  ~SkyCapturePass() override;

  //! Returns the captured cubemap texture.
  [[nodiscard]] auto GetCapturedCubemap() const
    -> observer_ptr<graphics::Texture>
  {
    return observer_ptr(captured_cubemap_.get());
  }

  //! Returns the shader-visible SRV slot for the captured cubemap.
  [[nodiscard]] auto GetCapturedCubemapSlot() const noexcept
    -> ShaderVisibleIndex override
  {
    return captured_cubemap_srv_;
  }

  //! Returns true if the capture has been performed at least once.
  [[nodiscard]] auto IsCaptured() const noexcept -> bool override
  {
    return is_captured_;
  }

  //! Returns a monotonic generation token that increases when the capture
  //! has been updated.
  [[nodiscard]] auto GetCaptureGeneration() const noexcept
    -> std::uint64_t override
  {
    return capture_generation_;
  }

  //! Marks the capture as dirty, forcing a re-capture on the next execution.
  auto MarkDirty() noexcept -> void { is_captured_ = false; }

protected:
  auto ValidateConfig() -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;

  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;

  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  //! Ensures internal capture resources are created.
  auto EnsureResourcesCreated() -> void;

  //! Sets up viewport and scissors for a single cubemap face.
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  observer_ptr<oxygen::Graphics> gfx_;
  std::shared_ptr<SkyCapturePassConfig> config_;

  std::shared_ptr<graphics::Texture> captured_cubemap_;
  graphics::NativeView captured_cubemap_srv_view_ {};
  ShaderVisibleIndex captured_cubemap_srv_ { kInvalidShaderVisibleIndex };

  //! RTVs for each cubemap face.
  std::vector<graphics::NativeView> face_rtvs_;
  //! Single framebuffer for the whole cubemap (used for clearing).
  std::shared_ptr<graphics::Framebuffer> all_faces_fb_;

  //! Constants for unprojecting each face.
  std::shared_ptr<graphics::Buffer> face_constants_buffer_;
  void* face_constants_mapped_ { nullptr };
  // Keep alive the native views for the 6 CBVs
  std::vector<graphics::NativeView> face_constants_cbvs_;
  // And their persistent indices
  std::vector<ShaderVisibleIndex> face_constants_indices_;

  std::uint64_t capture_generation_ { 1 };
  bool is_captured_ { false };

  //! Tracks the last known GPU resource state of the captured cubemap so that
  //! re-capture after MarkDirty() provides the correct initial state to the
  //! barrier tracker (avoiding RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH).
  graphics::ResourceStates cubemap_last_state_ {
    graphics::ResourceStates::kCommon
  };

  //! Tracks the last known GPU resource state of the face constants buffer.
  graphics::ResourceStates face_cb_last_state_ {
    graphics::ResourceStates::kCommon
  };
};

} // namespace oxygen::engine

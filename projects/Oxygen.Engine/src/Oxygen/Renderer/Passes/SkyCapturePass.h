//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
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
  [[nodiscard]] auto GetCapturedCubemap(ViewId view_id) const
    -> observer_ptr<graphics::Texture>
  {
    if (const auto it = capture_state_by_view_.find(view_id);
      it != capture_state_by_view_.end()) {
      return observer_ptr(it->second.captured_cubemap.get());
    }
    return nullptr;
  }

  //! Returns the shader-visible SRV slot for the captured cubemap.
  [[nodiscard]] auto GetCapturedCubemapSlot(ViewId view_id) const noexcept
    -> ShaderVisibleIndex override
  {
    if (const auto it = capture_state_by_view_.find(view_id);
      it != capture_state_by_view_.end()) {
      return it->second.captured_cubemap_srv;
    }
    return kInvalidShaderVisibleIndex;
  }

  //! Returns true if the capture has been performed at least once.
  [[nodiscard]] auto IsCaptured(ViewId view_id) const noexcept -> bool override
  {
    if (const auto it = capture_state_by_view_.find(view_id);
      it != capture_state_by_view_.end()) {
      return it->second.is_captured;
    }
    return false;
  }

  //! Returns a monotonic generation token that increases when the capture
  //! has been updated.
  [[nodiscard]] auto GetCaptureGeneration(ViewId view_id) const noexcept
    -> std::uint64_t override
  {
    if (const auto it = capture_state_by_view_.find(view_id);
      it != capture_state_by_view_.end()) {
      return it->second.capture_generation;
    }
    return 0;
  }

  //! Marks the capture as dirty, forcing a re-capture on the next execution.
  auto MarkDirty(ViewId view_id) noexcept -> void;

  auto EraseViewState(ViewId view_id) -> void;

protected:
  auto ValidateConfig() -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;

  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;

  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  struct CaptureState {
    std::shared_ptr<graphics::Texture> captured_cubemap;
    graphics::NativeView captured_cubemap_srv_view {};
    ShaderVisibleIndex captured_cubemap_srv { kInvalidShaderVisibleIndex };
    std::vector<graphics::NativeView> face_rtvs;
    std::shared_ptr<graphics::Framebuffer> all_faces_fb;
    std::shared_ptr<graphics::Buffer> face_constants_buffer;
    void* face_constants_mapped { nullptr };
    std::vector<graphics::NativeView> face_constants_cbvs;
    std::vector<ShaderVisibleIndex> face_constants_indices;
    std::uint64_t capture_generation { 1 };
    bool is_captured { false };
    graphics::ResourceStates cubemap_last_state {
      graphics::ResourceStates::kCommon
    };
    graphics::ResourceStates face_cb_last_state {
      graphics::ResourceStates::kCommon
    };
  };

  //! Ensures internal capture resources are created.
  auto EnsureResourcesCreated(ViewId view_id) -> CaptureState&;
  auto ReleaseStateResources(CaptureState& state) -> void;

  //! Sets up viewport and scissors for a single cubemap face.
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  observer_ptr<oxygen::Graphics> gfx_;
  std::shared_ptr<SkyCapturePassConfig> config_;

  std::unordered_map<ViewId, CaptureState> capture_state_by_view_;
};

} // namespace oxygen::engine

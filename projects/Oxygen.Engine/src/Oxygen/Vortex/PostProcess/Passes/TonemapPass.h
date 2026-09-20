//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
namespace internal {
  template <typename Payload> class PerViewStructuredPublisher;
}

namespace postprocess {

  class TonemapPass {
  public:
    struct Inputs {
      const graphics::Texture* scene_signal { nullptr };
      const graphics::Buffer* exposure_buffer { nullptr };
      const graphics::Buffer* frame_exposure_buffer { nullptr };
      ShaderVisibleIndex scene_signal_srv { kInvalidShaderVisibleIndex };
      ShaderVisibleIndex bloom_texture_srv { kInvalidShaderVisibleIndex };
      ShaderVisibleIndex exposure_buffer_srv { kInvalidShaderVisibleIndex };
      ShaderVisibleIndex frame_exposure_srv { kInvalidShaderVisibleIndex };
      observer_ptr<const graphics::Framebuffer> post_target;
      engine::ToneMapper tone_mapper { engine::ToneMapper::kAcesFitted };
      float exposure_value { 1.0F };
      float gamma { 2.2F };
      float bloom_intensity { 0.0F };
      std::optional<Vec3> background_color;
      const graphics::Texture* scene_fallback { nullptr };
      ShaderVisibleIndex scene_fallback_srv { kInvalidShaderVisibleIndex };
      const graphics::Buffer* conversion_report { nullptr };
      ShaderVisibleIndex conversion_report_srv { kInvalidShaderVisibleIndex };
    };

    struct RecordingState {
      bool requested { false };
      bool recorded { false };
      bool writes_visible_output { false };
    };

    OXGN_VRTX_API explicit TonemapPass(Renderer& renderer);
    OXGN_VRTX_API ~TonemapPass();

    TonemapPass(const TonemapPass&) = delete;
    auto operator=(const TonemapPass&) -> TonemapPass& = delete;
    TonemapPass(TonemapPass&&) = delete;
    auto operator=(TonemapPass&&) -> TonemapPass& = delete;

    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const Inputs& inputs)
      -> RecordingState;

  private:
    auto UpdatePassConstants(RenderContext& ctx, const Inputs& inputs)
      -> ShaderVisibleIndex;

    Renderer& renderer_;
    std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
      std::array<std::uint32_t, 16U>>>
      constants_publisher_;
    std::optional<frame::SequenceNumber> constants_frame_;
  };

} // namespace postprocess

} // namespace oxygen::vortex

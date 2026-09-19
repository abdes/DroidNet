//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Graphics/Direct3D12/Graphics.h>

namespace oxygen::vortex::testing::exposure {

class ExposureFailureGraphics final : public graphics::d3d12::Graphics {
public:
  using graphics::d3d12::Graphics::Graphics;
  std::shared_ptr<graphics::IShaderByteCode> tone_probe;
  auto GetShader(const graphics::ShaderRequest& request) const
    -> std::shared_ptr<graphics::IShaderByteCode> override;
  mutable std::weak_ptr<graphics::Texture> processed_sky;
  bool track_resources { false };
  bool account_texture_allocations { false };
  unsigned accounting_iteration { 0 };
  std::string accounting_phase;
  mutable std::uint64_t peak_texture_bytes { 0 };
  mutable std::uint64_t peak_hdr_bytes { 0 };
  mutable std::uint64_t peak_buffer_bytes { 0 };
  mutable std::uint64_t peak_placement_bytes { 0 };
  mutable std::uint64_t peak_engine_placement_bytes { 0 };
  mutable unsigned peak_hdr_iteration { 0 };
  mutable nlohmann::json allocation_peaks = nlohmann::json::object();
  mutable nlohmann::json allocation_peak_history = nlohmann::json::array();
  static auto IsExposureHdrTexture(std::string_view name) -> bool;
  mutable std::vector<std::weak_ptr<graphics::Texture>> tracked_textures;
  mutable std::vector<std::weak_ptr<graphics::Buffer>> tracked_buffers;
  auto MeasureTrackedPlacement() const -> nlohmann::json;
  auto ObserveAllocationPeak() const -> void;
  auto CreateBuffer(const graphics::BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override;
  auto CreateTexture(const graphics::TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture> override;
  std::vector<std::string> recorder_names;
  bool fail_next_exposure_recorder { false };
  bool fail_next_frame_recorder { false };
  bool fail_next_fallback_recorder { false };
  bool fail_status_recorder { false };
  std::string fail_recorder_name;
  bool defer_tonemap_recorders { false };
  std::vector<std::shared_ptr<const graphics::CommandList>>
    deferred_tonemap_recordings;
  bool fail_next_suitability_recorder { false };
  auto AcquireCommandRecorder(const graphics::QueueKey& queue,
    std::string_view name, bool immediate = true)
    -> std::unique_ptr<graphics::CommandRecorder,
      std::function<void(graphics::CommandRecorder*)>> override;
};

} // namespace oxygen::vortex::testing::exposure

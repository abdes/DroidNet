//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

// Single buffer provider with configurable mapping policy.
// Must be created via UploadCoordinator::CreateSingleBufferStaging.
class SingleBufferStaging final : public StagingProvider {
  friend class UploadCoordinator;

public:
  OXGN_RNDR_API explicit SingleBufferStaging(
    UploaderTag tag, observer_ptr<oxygen::Graphics> gfx, float slack = 0.5f);

  OXGN_RNDR_NDAPI auto Allocate(SizeBytes size, std::string_view debug_name)
    -> std::expected<Allocation, UploadError> override;

  auto RetireCompleted(UploaderTag, FenceValue completed) -> void override;

private:
  auto EnsureCapacity(uint64_t desired, std::string_view name)
    -> std::expected<void, UploadError>;
  auto Map() -> std::expected<void, UploadError>;
  auto UnMap() noexcept -> void;

  auto UpdateAllocationStats(SizeBytes size) noexcept -> void;

  observer_ptr<Graphics> gfx_;
  float slack_ { 0.5f };
  std::shared_ptr<graphics::Buffer> buffer_;
  std::byte* mapped_ptr_ { nullptr };
};

} // namespace oxygen::engine::upload

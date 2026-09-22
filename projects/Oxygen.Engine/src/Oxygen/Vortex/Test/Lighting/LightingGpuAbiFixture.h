//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>

namespace oxygen::vortex::testing {

//! Native wire decoding through production HLSL contracts and Graphics
//! readback.
class LightingGpuAbiTest
  : public graphics::d3d12::testing::ReadbackTestFixture {
protected:
  struct DecodeRequest {
    std::span<const std::byte> records;
    std::uint32_t stride { 0U };
    std::uint32_t record_kind { 0U };
    std::uint32_t decoded_words { 0U };
    std::uint32_t first_element { 0U };
    std::uint32_t count { 0U };
    ShaderVisibleIndex indices_srv { kInvalidShaderVisibleIndex };
    bool constant_buffer_records { false };
  };
  auto CreateBackend(const SerializedBackendConfig& config,
    const SerializedPathFinderConfig& paths)
    -> std::shared_ptr<graphics::d3d12::Graphics> override;
  auto BackendConfigJson() const -> std::string override;
  auto PathFinderConfigJson() const -> std::string override;
  auto TearDown() -> void override;

  auto Decode(const DecodeRequest& request) -> std::vector<std::uint32_t>;
  auto PublishIndices(std::span<const std::uint32_t> indices)
    -> ShaderVisibleIndex;

private:
  std::shared_ptr<graphics::Buffer> indices_buffer_;
};

} // namespace oxygen::vortex::testing

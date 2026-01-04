//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Headless/Internal/EngineShaders.h>

namespace oxygen::graphics::headless::internal {

namespace {
  // Create a deterministic 4-word blob from the stable request key. Embedded
  // here so we can avoid a separate HeadlessShader.* file.
  [[nodiscard]] auto MakeHeadlessBytecode(const uint64_t key)
    -> std::shared_ptr<IShaderByteCode>
  {
    std::vector<uint32_t> blob(4);
    blob[0] = static_cast<uint32_t>(key & 0xffffffffu);
    blob[1] = static_cast<uint32_t>((key >> 32) & 0xffffffffu);
    blob[2] = ~blob[0];
    blob[3] = ~blob[1];

    return std::make_shared<ShaderByteCode<std::vector<uint32_t>>>(
      std::move(blob));
  }

} // namespace

[[nodiscard]] auto EngineShaders::GetShader(const ShaderRequest& request) const
  -> std::shared_ptr<IShaderByteCode>
{
  const auto canonical = CanonicalizeShaderRequest(ShaderRequest(request));
  const auto key = ComputeShaderRequestKey(canonical);

  auto it = cache_.find(key);
  if (it != cache_.end()) {
    return it->second;
  }
  auto bc = MakeHeadlessBytecode(key);
  cache_.emplace(key, bc);
  return bc;
}

EngineShaders::EngineShaders()
{
  LOG_F(INFO, "Headless EngineShaders pre-warming engine shaders");

  try {
    static const std::array<ShaderRequest, 5> kEngineShaderRequests = {
      ShaderRequest {
        .stage = oxygen::ShaderType::kVertex,
        .source_path = "FullScreenTriangle.hlsl",
        .entry_point = "VS",
      },
      ShaderRequest {
        .stage = oxygen::ShaderType::kPixel,
        .source_path = "FullScreenTriangle.hlsl",
        .entry_point = "PS",
      },
      ShaderRequest {
        .stage = oxygen::ShaderType::kVertex,
        .source_path = "DepthPrePass.hlsl",
        .entry_point = "VS",
      },
      ShaderRequest {
        .stage = oxygen::ShaderType::kPixel,
        .source_path = "DepthPrePass.hlsl",
        .entry_point = "PS",
      },
      ShaderRequest {
        .stage = oxygen::ShaderType::kCompute,
        .source_path = "LightCulling.hlsl",
        .entry_point = "CS",
      },
    };

    for (const auto& req : kEngineShaderRequests) {
      [[maybe_unused]] auto bc = GetShader(req);
    }
  } catch (...) {
    LOG_F(WARNING, "EngineShaders pre-warm failed (continuing)");
  }
}

} // namespace oxygen::graphics::headless::internal

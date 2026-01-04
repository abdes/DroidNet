//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/Internal/EngineShaders.h>

namespace oxygen::graphics::headless::internal {

namespace {
  // Create a deterministic 4-word blob from the string hash. Embedded here so
  // we can avoid a separate HeadlessShader.* file.
  [[nodiscard]] auto MakeHeadlessBytecode(std::string_view unique_id)
    -> std::shared_ptr<IShaderByteCode>
  {
    const uint64_t h = std::hash<std::string_view> {}(unique_id);
    std::vector<uint32_t> blob(4);
    blob[0] = static_cast<uint32_t>(h & 0xffffffffu);
    blob[1] = static_cast<uint32_t>((h >> 32) & 0xffffffffu);
    blob[2] = ~blob[0];
    blob[3] = ~blob[1];

    return std::make_shared<ShaderByteCode<std::vector<uint32_t>>>(
      std::move(blob));
  }

} // namespace

[[nodiscard]] auto EngineShaders::GetShader(std::string_view id) const
  -> std::shared_ptr<IShaderByteCode>
{
  const std::string key(id);
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    return it->second;
  }
  auto bc = MakeHeadlessBytecode(id);
  cache_.emplace(key, bc);
  return bc;
}

EngineShaders::EngineShaders()
{
  LOG_F(INFO, "Headless EngineShaders pre-warming engine shaders");

  try {
    static constexpr std::array<std::string_view, 5> kEngineShaderIds = {
      std::string_view("VS@FullScreenTriangle.hlsl#VS"),
      std::string_view("PS@FullScreenTriangle.hlsl#PS"),
      std::string_view("VS@DepthPrePass.hlsl#VS"),
      std::string_view("PS@DepthPrePass.hlsl#PS"),
      std::string_view("CS@LightCulling.hlsl#CS"),
    };

    for (const auto id : kEngineShaderIds) {
      [[maybe_unused]] auto bc = GetShader(id);
    }
  } catch (...) {
    LOG_F(WARNING, "EngineShaders pre-warm failed (continuing)");
  }
}

} // namespace oxygen::graphics::headless::internal

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine::internal {

struct BrdfLutParams {
  uint32_t resolution { 0u };
  uint32_t sample_count { 0u };
};

inline constexpr BrdfLutParams kDefaultBrdfLutParams { 256u, 128u };

struct LutResult {
  std::shared_ptr<graphics::Texture> texture;
  ShaderVisibleIndex index { kInvalidShaderVisibleIndex };

  static constexpr auto Err() noexcept
  {
    return LutResult {
      .texture = nullptr,
      .index = ShaderVisibleIndex { kInvalidShaderVisibleIndex },
    };
  }
};

class IBrdfLutProvider {
public:
  virtual ~IBrdfLutProvider() = default;

  [[nodiscard]] virtual auto GetOrCreateLut(
    BrdfLutParams params = kDefaultBrdfLutParams) -> LutResult
    = 0;
};

} // namespace oxygen::engine::internal

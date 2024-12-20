//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/base/compilers.h"
#include "oxygen/base/logging.h"
#include "oxygen/base/types.h"

namespace oxygen::graphics {

  // Supported backend implementations for the renderer.
  enum class GraphicsBackendType : uint8_t
  {
    kDirect3D12 = 0,
    kVulkan = 1,
  };

  [[nodiscard]] constexpr  auto to_string(const GraphicsBackendType value) -> std::string
  {
    switch (value) {
    case GraphicsBackendType::kDirect3D12: return "Direct3D12";
    case GraphicsBackendType::kVulkan: return "Vulkan 1.3";
    }
    OXYGEN_UNREACHABLE_RETURN("_UNKNOWN_");
  }
  inline auto operator<<(std::ostream& out, const GraphicsBackendType value) -> auto&
  {
    return out << to_string(value);
  }

  extern "C" {
    typedef void* (*GetRendererModuleInterfaceFunc)();

    typedef void* (*CreateRendererFunc)();
    typedef void (*DestroyRendererFunc)(void*);

    struct RendererModuleInterface
    {
      // ReSharper disable once CppInconsistentNaming
      CreateRendererFunc CreateRenderer;
      // ReSharper disable once CppInconsistentNaming
      DestroyRendererFunc DestroyRenderer;
    };
  };

}  // namespace oxygen::renderer

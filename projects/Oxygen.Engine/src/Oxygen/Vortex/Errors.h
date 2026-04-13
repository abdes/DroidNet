//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <system_error>

namespace oxygen::vortex {

enum class RendererError : int {
  kSurfaceExpired,
};

class RendererErrorCategory : public std::error_category {
public:
  const char* name() const noexcept override { return "Vortex Renderer Error"; }

  std::string message(int ev) const override
  {
    switch (static_cast<RendererError>(ev)) {
    case RendererError::kSurfaceExpired:
      return "The target surface for rendering is no longer valid";
    default:
      return "Unknown Vortex renderer subsystem error";
    }
  }
};

inline const RendererErrorCategory& GetRendererErrorCategory() noexcept
{
  static RendererErrorCategory instance;
  return instance;
}

inline std::error_code make_error_code(RendererError e) noexcept
{
  return { static_cast<int>(e), GetRendererErrorCategory() };
}

} // namespace oxygen::vortex

template <>
struct std::is_error_code_enum<oxygen::vortex::RendererError> : true_type { };

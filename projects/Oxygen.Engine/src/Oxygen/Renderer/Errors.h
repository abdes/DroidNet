//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <system_error>

namespace oxygen::renderer {

//! Domain-specific Renderer error codes exposed as std::error_code.
enum class RendererError : int {
  kSurfaceExpired,
};

//! Category for renderer errors.
class RendererErrorCategory : public std::error_category {
public:
  const char* name() const noexcept override { return "Renderer Error"; }

  std::string message(int ev) const override
  {
    switch (static_cast<RendererError>(ev)) {
    case RendererError::kSurfaceExpired:
      return "The target surface for rendering is no longer valid";
    default:
      return "Unknown renderer subsystem error";
    }
  }
};
// Forward declare category accessor.
inline const RendererErrorCategory& GetRendererErrorCategory() noexcept
{
  static RendererErrorCategory instance;
  return instance;
}

// Helper to create std::error_code from RendererError
inline std::error_code make_error_code(RendererError e) noexcept
{
  return { static_cast<int>(e), GetRendererErrorCategory() };
}

} // namespace oxygen::renderer

// Inject std::is_error_code_enum specialization into std so that
// RendererError can be implicitly converted to std::error_code.
template <>
struct std::is_error_code_enum<oxygen::renderer::RendererError> : true_type { };

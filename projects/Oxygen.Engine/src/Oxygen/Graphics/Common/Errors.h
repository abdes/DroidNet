//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <system_error>

namespace oxygen::graphics {

//! Domain-specific graphics error codes exposed as std::error_code.
enum class GraphicsError : int {
  kResourceCreationFailed = 1,
  kDescriptorAllocationFailed = 2,
  kResourceRegistrationFailed = 3
};

//! Category for graphics errors.
class GraphicsErrorCategory : public std::error_category {
public:
  const char* name() const noexcept override { return "Graphics Error"; }

  std::string message(int ev) const override
  {
    switch (static_cast<GraphicsError>(ev)) {
    case GraphicsError::kResourceCreationFailed:
      return "Failed to create resource";
    case GraphicsError::kDescriptorAllocationFailed:
      return "Failed to allocate descriptor";
    case GraphicsError::kResourceRegistrationFailed:
      return "Failed to register resource";
    default:
      return "Unknown graphics error";
    }
  }
};
// Forward declare category accessor.
inline const GraphicsErrorCategory& GetGraphicsErrorCategory() noexcept
{
  static GraphicsErrorCategory instance;
  return instance;
}

// Helper to create std::error_code from GraphicsErrc
inline std::error_code make_error_code(GraphicsError e) noexcept
{
  return { static_cast<int>(e), GetGraphicsErrorCategory() };
}

} // namespace oxygen::graphics

// Inject std::is_error_code_enum specialization into std so that
// GraphicsError can be implicitly converted to std::error_code.
template <>
struct std::is_error_code_enum<oxygen::graphics::GraphicsError> : true_type { };

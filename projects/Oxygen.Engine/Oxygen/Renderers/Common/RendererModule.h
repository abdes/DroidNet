//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// ReSharper disable once CppUnusedIncludeDirective (unreachable code)
#include "Oxygen/Base/Compilers.h"
#include "Oxygen/Base/Types.h"

namespace oxygen::graphics {

//! Possible graphic backend types for the renderer.
enum class GraphicsBackendType : uint8_t {
  kDirect3D12 = 0, //!< Direct3D12 backend.
  kVulkan = 1, //!< Vulkan backend.
};

constexpr auto kGetRendererModuleApi = "GetRendererModuleApi";

// Graphics backend API, loadable from a DLL.
extern "C" {
//! Entry point to get the renderer module API.
typedef void* (*GetRendererModuleApiFunc)();

typedef void* (*CreateRendererFunc)();
typedef void (*DestroyRendererFunc)();

//! Interface for the renderer module.
struct RendererModuleApi {
  // ReSharper disable CppInconsistentNaming

  //! Create a new renderer instance.
  /*!
    This function is called by the renderer loader to create a renderer
    instance, which is then initialized through Renderer::Init. A backend
    implementation module will typically make the renderer instance
    available as a shared pointer, suitable for use inside and outside of
    the renderer module.

    The renderer loader offers an easy way to get the renderer instance as
    a smart pointer by calling GetRenderer(), which is the recommended way
    to keep a reference to the created renderer instance.
   */
  CreateRendererFunc CreateRenderer;

  //! Destroy the renderer instance.
  /*!
    This function is called by the renderer loader to destroy the renderer
    instance, after calling its Renderer::Shutdown() method. It is required
    that after a call to this function, all shared pointers referring to
    the render instance are invalidated.
   */
  DestroyRendererFunc DestroyRenderer;

  // ReSharper restore CppInconsistentNaming
};
};

//! Convert GraphicsBackendType to string.
/**
  \param value The GraphicsBackendType value to convert.
  \return A string representation of the GraphicsBackendType value.
 */
[[nodiscard]]
constexpr auto to_string(const GraphicsBackendType value) -> std::string
{
  switch (value) {
  case GraphicsBackendType::kDirect3D12:
    return "Direct3D12";
  case GraphicsBackendType::kVulkan:
    return "Vulkan 1.3";
  }
  OXYGEN_UNREACHABLE_RETURN("_UNKNOWN_");
}

//! Output stream operator for GraphicsBackendType.
/*!
  \param out The output stream.
  \param value The GraphicsBackendType value to output.
  \return The output stream.
 */
inline auto operator<<(std::ostream& out, const GraphicsBackendType value) -> auto&
{
  return out << to_string(value);
}

} // namespace oxygen::renderer

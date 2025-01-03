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
enum class BackendType : uint8_t {
  kDirect3D12 = 0, //!< Direct3D12 backend.
  kVulkan = 1, //!< Vulkan backend.
};

constexpr auto kGetGraphicsModuleApi = "GetGraphicsModuleApi";

// Graphics backend API, loadable from a DLL.
extern "C" {

//! Entry point to get the renderer module API.
typedef void* (*GetGraphicsModuleApiFunc)();

typedef void* (*CreateBackendFunc)();
typedef void (*DestroyBackendFunc)();
//
// typedef bool (*InitializeFunc)();
// typedef void (*ShutdownFunc)();

//! Interface for the renderer module.
struct GraphicsModuleApi {
  // ReSharper disable CppInconsistentNaming

  //! Create the backend instance.
  /*!
    This function will be called by the loader once to create a backend
    instance, and will not be called again until the backend is destroyed.

    A backend implementation will typically make the backend instance available
    as a shared pointer, suitable for use inside and outside of the module.

    The loader offers a public and easy way to get the backend instance as a
    smart pointer by calling GetBackend(), which is the recommended way to keep
    a reference to a loaded.
   */
  CreateBackendFunc CreateBackend;

  //! Destroy the backend instance.
  /*!
    This function is called by the loader to destroy the backend instance
    created through `CreateRenderer`. The backend is eventually shutdown if it
    has not been before this function is called.

    \note It is required that after a call to this function, all shared pointers
    referring to the backend instance are invalidated.
   */
  DestroyBackendFunc DestroyBackend;

  ////! Initialize the graphics backend module.
  ///*!
  // Typically involves the discovery of available adapters, displays, and other
  // hardware devices, as well as the initialization of the graphics API. It should
  // not create a renderer instance yet, which is done through CreateRenderer.
  // */
  // InitializeFunc Initialize;

  ////! Shutdown the graphics backend module.
  // InitializeFunc Shutdown;

  // ReSharper restore CppInconsistentNaming
};

}; // extern "C"

//! Convert GraphicsBackendType to string.
/**
  \param value The GraphicsBackendType value to convert.
  \return A string representation of the GraphicsBackendType value.
 */
[[nodiscard]]
constexpr auto to_string(const BackendType value) -> std::string
{
  switch (value) {
  case BackendType::kDirect3D12:
    return "Direct3D12";
  case BackendType::kVulkan:
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
inline auto operator<<(std::ostream& out, const BackendType value) -> auto&
{
  return out << to_string(value);
}

} // namespace oxygen::graphics

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/GraphicsCapabilities.h>

namespace oxygen::graphics {

//! Possible graphic backend types for the renderer.
enum class BackendType : uint8_t {
  //! Direct3D12 backend.
  kDirect3D12 = 0,
  //! Vulkan backend.
  kVulkan = 1,
  //! Headless backend.
  kHeadless = 2,
};

constexpr auto kGetGraphicsModuleApi = "GetGraphicsModuleApi";

// Graphics backend API, loadable from a DLL.
extern "C" {

//! Entry point to get the renderer module API.
using GetGraphicsModuleApiFunc = void* (*)();

using CreateBackendFunc
  = void* (*)(const oxygen::SerializedBackendConfig& config,
    const oxygen::SerializedPathFinderConfig& path_finder_config);
using DestroyBackendFunc = void (*)();

//! What a hardware adapter's device supports.
struct GraphicsAdapterFeaturesRecord {
  //! The feature level as the API names it, e.g. `12_2`.
  const char* feature_level;
  //! The shader model, e.g. `6.8`.
  const char* shader_model;
  std::uint32_t binding_tier;
  bool wave_ops;
  MemoryArchitecture memory_architecture;
  //! The ray tracing tier, e.g. `1.1`; empty when unsupported.
  const char* ray_tracing;
  //! The variable-rate shading tier; 0 when unsupported.
  std::uint32_t variable_shading_rate;
  //! The mesh shader tier; 0 when unsupported.
  std::uint32_t mesh_shaders;
  //! The sampler feedback tier, e.g. `1.0`; empty when unsupported.
  const char* sampler_feedback;
};

//! One hardware adapter as the backend sees it; everything it points to lives
//! until the visitor returns.
struct GraphicsAdapterRecord {
  const char* name;
  std::uint32_t vendor_id;
  std::uint32_t device_id;
  std::uint64_t dedicated_memory;
  bool has_display;
  //! Null when no device could be created to query the adapter.
  const GraphicsAdapterFeaturesRecord* features;
  //! The requirements the adapter misses, each with what it needs and what
  //! the device has; none when it meets them all.
  const char* const* missing_requirements;
  std::size_t missing_requirement_count;
  //! The adapter the backend selects without hints.
  bool default_choice;
};

using AdapterVisitorFunc
  = void (*)(void* context, const GraphicsAdapterRecord& adapter);
//! Visit every hardware adapter; false when enumeration failed.
using EnumerateAdaptersFunc = bool (*)(void* context, AdapterVisitorFunc visit);

//! Interface for the renderer module.
struct GraphicsModuleApi {

  //! Create the backend instance.
  /*!
   This function will be called by the loader once to create a backend instance,
   and will not be called again until the backend is destroyed.

   A backend implementation will typically make the backend instance available
   as a shared pointer, suitable for use inside and outside of the module.

   The loader offers a public and easy way to get the backend instance as a
   smart pointer by calling GetBackend(), which is the recommended way to keep a
   reference to a loaded.
  */
  CreateBackendFunc CreateBackend;

  //! Destroy the backend instance.
  /*!
   This function is called by the loader to destroy the backend instance created
   through `CreateBackend`. The backend is eventually shutdown if it has not
   been before this function is called.

   @note It is required that after a call to this function, all shared pointers
   referring to the backend instance are invalidated.
  */
  DestroyBackendFunc DestroyBackend;

  //! Enumerate the hardware adapters without creating a backend instance.
  /*!
   Callable whether or not a backend instance exists; it creates no device
   and changes no backend state.
  */
  EnumerateAdaptersFunc EnumerateAdapters;
};

}; // extern "C"

//! Convert GraphicsBackendType to string.
/**
  \param value The GraphicsBackendType value to convert. \return A string
  representation of the GraphicsBackendType value.
 */
[[nodiscard]]
constexpr auto to_string(const BackendType value) -> std::string
{
  switch (value) {
  case BackendType::kDirect3D12:
    return "Direct3D12";
  case BackendType::kVulkan:
    return "Vulkan 1.3";
  case BackendType::kHeadless:
    return "Headless";
  }
  OXYGEN_UNREACHABLE_RETURN("_UNKNOWN_");
}

} // namespace oxygen::graphics

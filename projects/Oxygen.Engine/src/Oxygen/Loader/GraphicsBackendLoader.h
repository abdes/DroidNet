//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/GraphicsCapabilities.h>

namespace oxygen {

class Graphics;
struct GraphicsConfig;

namespace loader {
  //! Exception thrown when the loader is used from a module other than the
  //! main executable module, before it has been first initialized from the
  //! main executable module.
  class InvalidOperationError final : public std::logic_error {
    using std::logic_error::logic_error;
  };

  namespace detail {
    class PlatformServices;
  } // namespace detail

  //! What a hardware adapter's device supports.
  struct GraphicsAdapterFeatures {
    //! The feature level as the API names it, e.g. `12_2`.
    std::string feature_level;
    //! The shader model, e.g. `6.8`.
    std::string shader_model;
    std::uint32_t binding_tier { 0U };
    bool wave_ops { false };
    graphics::MemoryArchitecture memory_architecture {
      graphics::MemoryArchitecture::kDedicated,
    };
    //! The ray tracing tier, e.g. `1.1`; none when unsupported.
    std::optional<std::string> ray_tracing;
    //! The variable-rate shading tier; none when unsupported.
    std::optional<std::uint32_t> variable_shading_rate;
    //! The mesh shader tier; none when unsupported.
    std::optional<std::uint32_t> mesh_shaders;
    //! The sampler feedback tier, e.g. `1.0`; none when unsupported.
    std::optional<std::string> sampler_feedback;
  };

  //! One hardware adapter a graphics backend can render on.
  struct GraphicsAdapter {
    std::string name;
    std::uint32_t vendor_id { 0U };
    std::uint32_t device_id { 0U };
    std::uint64_t dedicated_memory { 0U };
    bool has_display { false };
    //! None when no device could be created to query the adapter.
    std::optional<GraphicsAdapterFeatures> features;
    //! The requirements the adapter misses, each with what it needs and what
    //! the device has; empty when it meets them all.
    std::vector<std::string> missing_requirements;
    //! The adapter the backend selects without hints.
    bool default_choice { false };
  };
} // namespace loader

//! A singleton class that dynamically loads and unloads a graphics backend.
/*!
 This loader imposes the restriction (to ensure a single instance across the
 process) that it should be first initialized from the main executable module.
 Any attempt to access the single instance of the loader from another module
 before it has been initialized will result in a `InvalidOperationError`
 exception.

 For testability purposes, the loader can be constructed with a custom
 `PlatformServices` implementation. This allows to inject a mock implementation
 during testing. If no custom implementation is provided, the default platform
 services implementation will be used.
*/
class GraphicsBackendLoader {
public:
  //! Gets the singleton instance of the graphics backend loader.
  /*!\n   \n   Mutually exclusive with `GetInstanceRelaxed()`. Once one of the
   * two\n   retrieval methods initializes the singleton, the other cannot be
   * used and\n   will throw `loader::InvalidOperationError`. Use this strict
   * variant when the\n   first initialization is guaranteed to occur from the
   * main executable module.\n  */
  static auto GetInstance(
    std::shared_ptr<loader::detail::PlatformServices> platform_services
    = nullptr) -> GraphicsBackendLoader&;

  //! Gets a singleton instance with relaxed initialization rules.
  /*!\n\n   Mutually exclusive with `GetInstance()`. This relaxed variant allows
   * the\n   first call to originate from any module (recording that module as
   * the\n   owner). All subsequent calls (including resets) must originate from
   * the same\n   module or an `loader::InvalidOperationError` is thrown. Choose
   * this when\n   initialization might legitimately occur inside a dynamically
   * loaded module.\n  */
  static auto GetInstanceRelaxed(
    std::shared_ptr<loader::detail::PlatformServices> platform_services
    = nullptr) -> GraphicsBackendLoader&;

  //! Requests close and relinquishes this facade's active backend owner.
  ~GraphicsBackendLoader();

  OXYGEN_MAKE_NON_COPYABLE(GraphicsBackendLoader)
  OXYGEN_MAKE_NON_MOVABLE(GraphicsBackendLoader)

  //! Loads the specified graphics backend from a dynamically loadable module.
  [[nodiscard]] auto LoadBackend(graphics::BackendType backend,
    const GraphicsConfig& config,
    const PathFinderConfig& path_finder_config) const
    -> std::weak_ptr<Graphics>;

  //! Closes admission and drains the backend on its owner thread. External
  //! owners and native inspection references may retain the closed incarnation;
  //! reload reports BackendRetiring until their actual destruction and module
  //! release.
  void UnloadBackend() const noexcept;

  //! The hardware adapters of `backend`, in its enumeration order.
  /*!
   Loads the backend module without creating a backend instance.

   @throw std::runtime_error When the module cannot be loaded or the
   enumeration fails.
  */
  [[nodiscard]] auto ListAdapters(graphics::BackendType backend) const
    -> std::vector<loader::GraphicsAdapter>;

  //! Gets the backend instance if one is currently loaded.
  [[nodiscard]] auto GetBackend() const noexcept -> std::weak_ptr<Graphics>;

private:
  // Internal constructor capturing the originating module handle. The
  // originating module's directory is used as the primary base path for
  // resolving backend dynamic modules (falling back to the executable
  // directory if it cannot be determined).
  explicit GraphicsBackendLoader(void* origin_module,
    std::shared_ptr<loader::detail::PlatformServices> platform_services);

  class Impl;
  std::unique_ptr<Impl> pimpl_;
};

} // namespace oxygen

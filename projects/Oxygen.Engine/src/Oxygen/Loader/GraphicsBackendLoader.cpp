//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ReturnAddress.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Loader/Detail/PlatformServices.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

using oxygen::GraphicsBackendLoader;
using oxygen::GraphicsConfig;
using oxygen::SerializedBackendConfig;
using oxygen::graphics::BackendType;
using oxygen::graphics::GetGraphicsModuleApiFunc;
using oxygen::graphics::GraphicsModuleApi;
using oxygen::graphics::kGetGraphicsModuleApi;
using oxygen::loader::detail::PlatformServices;

namespace {

//! Gets the DLL filename for a graphics backend module.
/*!
 Constructs the appropriate DLL filename based on the backend type. The filename
 includes debug suffix for debug builds and uses the standard Oxygen.Graphics
 module naming convention.

 @param backend The backend type to get the module name for.
 @return The DLL filename for the specified backend.
 @throw std::runtime_error if the backend type is not yet implemented.

 ### Usage Examples

 ```cpp
 auto dll_name = GetBackendModuleDllName(BackendType::kDirect3D12);
 // Returns "Oxygen.Graphics.Direct3D12-d.dll" in debug builds
 ```
*/
auto GetBackendModuleDllName(const BackendType backend) -> std::string
{
  std::string engine_name;
  switch (backend) {
  case BackendType::kDirect3D12:
    engine_name = "Direct3D12";
    break;
  case BackendType::kHeadless:
    engine_name = "Headless";
    break;
  case BackendType::kVulkan:
    throw std::runtime_error(
      std::string("backend not yet implemented: ") + nostd::to_string(backend));
  }
  return "Oxygen.Graphics." + engine_name +
#if !defined(NDEBUG)
    "-d" +
#endif
    ".dll";
}

//! Serializes graphics configuration to JSON format for backend initialization.
/*!
 Converts a GraphicsConfig struct and backend type into a JSON string that can
 be passed to the backend module for initialization. Handles optional fields and
 merges extra configuration JSON properly.

 @param config The graphics configuration to serialize.
 @param backend_type The backend type to include in the JSON.
 @return A JSON string representation of the configuration.

 ### Usage Examples

 ```cpp
 GraphicsConfig config{};
 config.enable_debug = true;
 auto json = SerializeConfigToJson(config, BackendType::kDirect3D12);
 // Returns properly formatted JSON for backend initialization
 ```
*/
auto SerializeConfigToJson(
  const GraphicsConfig& config, BackendType backend_type) -> std::string
{
  // Start building the JSON object
  std::string json = "{\n";

  // Add backend type
  json += R"(  "backend_type": ")" + nostd::to_string(backend_type) + "\",\n";

  // Add basic boolean properties
  json += "  \"enable_debug\": "
    + std::string(config.enable_debug ? "true" : "false") + ",\n";
  json += "  \"enable_validation\": "
    + std::string(config.enable_validation ? "true" : "false") + ",\n";
  json += "  \"headless\": " + std::string(config.headless ? "true" : "false")
    + ",\n";
  json += "  \"enable_imgui\": "
    + std::string(config.enable_imgui ? "true" : "false") + ",\n";
  json += "  \"enable_vsync\": "
    + std::string(config.enable_vsync ? "true" : "false") + ",\n";

  // Add optional card name if present
  if (config.preferred_card_name.has_value()) {
    json += R"(  "preferred_card_name": ")" + config.preferred_card_name.value()
      + "\",\n";
  }

  // Add optional device ID if present
  if (config.preferred_card_device_id.has_value()) {
    json += R"(  "preferred_card_device_id": )"
      + std::to_string(config.preferred_card_device_id.value()) + ",\n";
  }

  // Add extra configuration (removing the enclosing braces)
  std::string extra = config.extra;
  if (extra == "{}" || extra.empty()) {
    // Remove trailing comma if there's no extra config
    if (json.size() > 2) {
      json.pop_back(); // Remove newline
      json.pop_back(); // Remove comma
      json += "\n";
    }
  } else {
    // Extract content from between braces and append
    size_t start = extra.find('{');
    size_t end = extra.rfind('}');
    if (start != std::string::npos && end != std::string::npos && start < end) {
      std::string content = extra.substr(start + 1, end - start - 1);
      // Trim leading/trailing whitespace
      content.erase(0, content.find_first_not_of(" \n\r\t"));
      content.erase(content.find_last_not_of(" \n\r\t") + 1);

      if (!content.empty()) {
        json += "  " + content + "\n";
      }
    }
  }

  // Close JSON object
  json += "}";

  return json;
}

} // namespace

// Implementation class that handles all the details
class GraphicsBackendLoader::Impl {
public:
  explicit Impl(PlatformServices::ModuleHandle origin_module,
    std::shared_ptr<PlatformServices> services = nullptr)
    : origin_module_(origin_module)
    , platform_services(
        services ? std::move(services) : std::make_shared<PlatformServices>())
  {
  }

  ~Impl() { UnloadBackend(); }

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto LoadBackend(const BackendType backend, const GraphicsConfig& config)
    -> GraphicsPtr
  {
    if (backend_instance) {
      LOG_F(WARNING,
        "A graphics backend has already been loaded; call UnloadBackend() "
        "first...");
      return backend_instance;
    }

    try {
      if (backend_module == nullptr) {
        // We expect the backend module to be in the same directory as the
        // executable.
        const auto module_name = GetBackendModuleDllName(backend);
        // Prefer origin module directory; fallback to executable directory.
        std::string base_dir
          = platform_services->GetModuleDirectory(origin_module_);
        if (base_dir.empty()) {
          base_dir = platform_services->GetExecutableDirectory();
        }
        LOG_F(INFO, "Using base directory for backend modules: {}", base_dir);
        const auto full_path = base_dir + module_name;

        // Load the module directly
        backend_module = platform_services->LoadModule(full_path);
        LOG_F(INFO, "Graphics backend for `{}` loaded from module `{}`",
          nostd::to_string(backend), module_name);
      }

      // Use the type-safe function address retrieval
      auto get_api
        = platform_services->GetFunctionAddress<GetGraphicsModuleApiFunc>(
          backend_module, kGetGraphicsModuleApi);
      auto* const backend_api = static_cast<GraphicsModuleApi*>(get_api());

      // Create the backend instance
      CreateBackendInstance(backend_api, backend, config);
      return backend_instance;
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "Failed to load graphics backend: {}", ex.what());
      backend_instance.reset();
      // NB: Do not close the module here as it may still be required until the
      // exception handling frames are complete. The module, if opened, will be
      // reused for subsequent calls to `LoadBackend` or will be unloaded if a
      // call to `UnloadBackend` is made, or when the loader is destroyed.
      throw;
    }
  }

  auto UnloadBackend() noexcept -> void
  {
    if (backend_module == nullptr) {
      DCHECK_EQ_F(backend_instance, nullptr);
      return;
    }

    backend_instance.reset();

    // Unload the backend module if it was loaded.
    try {
      platform_services->CloseModule(backend_module);
      backend_module = nullptr;
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "Error unloading backend module: {}", ex.what());
    }
  }

  [[nodiscard]] auto GetBackend() noexcept -> std::weak_ptr<Graphics>
  {
    CHECK_NOTNULL_F(backend_instance,
      "No graphics backend instance has been created; call LoadBackend() "
      "first...");

    return backend_instance;
  }

  [[nodiscard]] auto GetPlatformServices() const
    -> const std::shared_ptr<PlatformServices>&
  {
    return platform_services;
  }

private:
  auto CreateBackendInstance(GraphicsModuleApi* backend_api,
    const BackendType backend_type, const GraphicsConfig& config) -> void
  {
    if (!backend_instance) {
      // Create the JSON configuration
      const std::string config_json
        = SerializeConfigToJson(config, backend_type);

      // Create the configuration struct
      SerializedBackendConfig serialized_config;
      serialized_config.json_data = config_json.c_str();
      serialized_config.size = config_json.length();

      // Call the backend create function with the configuration
      void* instance = backend_api->CreateBackend(serialized_config);

      if (instance == nullptr) {
        throw std::runtime_error("Failed to create backend instance");
      }

      // Store the instance with a custom deleter that will call the destroy
      // function
      backend_instance = std::shared_ptr<Graphics>(
        static_cast<Graphics*>(instance),
        [destroyFunc = backend_api->DestroyBackend](const Graphics* instance) {
          if (instance != nullptr) {
            destroyFunc();
          }
        });
    }
  }

  // Member variables
  std::shared_ptr<Graphics> backend_instance;
  PlatformServices::ModuleHandle backend_module { nullptr };
  PlatformServices::ModuleHandle origin_module_ { nullptr };
  std::shared_ptr<PlatformServices> platform_services;
};

namespace {

//! Enforces the restriction that certain functions can only be called from the
//! main module.
/*!
 Validates that the calling function is executing from the main executable
 module and not from a dynamically loaded module. This is used to ensure
 singleton integrity across module boundaries.

 @param platform_services The platform services instance to use for validation.
 @param functionName The name of the function being called (for error messages).
 @param returnAddress The return address to check the module for.
 @throw InvalidOperationError if the call originates from a non-main module.

 ### Performance Characteristics

 - Time Complexity: O(1) - single module lookup
 - Memory: No allocation
 - Optimization: Uses fast platform-specific module resolution

 ### Usage Examples

 ```cpp
 EnforceMainModuleRestriction(services, "LoadBackend",
                              oxygen::ReturnAddress<>());
 ```
*/
auto EnforceMainModuleRestriction(
  const std::shared_ptr<PlatformServices>& platform_services,
  const char* functionName, void* returnAddress) -> void
{
  PlatformServices::ModuleHandle moduleHandle
    = platform_services->GetModuleHandleFromReturnAddress(returnAddress);
  if (!platform_services->IsMainExecutableModule(moduleHandle)) {
    throw oxygen::loader::InvalidOperationError(
      fmt::format("Function `{}` called from non-main module", functionName));
  }
}

// Shared initialization mode state (internal linkage)
enum class LoaderInitMode { kUninitialized, kStrict, kRelaxed };
static LoaderInitMode g_loader_init_mode = LoaderInitMode::kUninitialized;

} // namespace

/*!
 Gets the singleton instance of the graphics backend loader with optional
 platform services injection. This method enforces the restriction that it must
 first be called from the main executable module to ensure singleton integrity
 across module boundaries.

 @param platform_services An optional custom platform services implementation to
                          use. When not provided, the default platform services
                          implementation will be used. Can be used to reset the
                          loader for testing purposes.
 @return A reference to the singleton GraphicsBackendLoader instance.
 @throw InvalidOperationError if called from a non-main module before first
                              initialization from the main module, or if trying
                              to reset from a non-main module.

 ### Performance Characteristics

 - Time Complexity: O(1) - static initialization on first call
 - Memory: Single static instance allocation
 - Optimization: Uses static local variables for thread-safe initialization

 ### Usage Examples

 ```cpp
 // First call from main module
 auto& loader = GraphicsBackendLoader::GetInstance();

 // Testing with custom services
 auto mock_services = std::make_shared<MockPlatformServices>();
 auto& test_loader = GraphicsBackendLoader::GetInstance(mock_services);
 ```

 @note This method allows resetting the platform services for testing, but only
       when called from the main executable module.
*/
auto GraphicsBackendLoader::GetInstance(
  std::shared_ptr<PlatformServices> platform_services) -> GraphicsBackendLoader&
{
  // Enforce mutual exclusivity with relaxed variant.
  if (g_loader_init_mode == LoaderInitMode::kRelaxed) {
    LOG_F(ERROR,
      "GraphicsBackendLoader already initialized in relaxed mode; cannot call "
      "GetInstance (strict) afterwards");
    throw loader::InvalidOperationError(
      "GetInstance called after GetInstanceRelaxed initialization");
  }

  static bool first_call = true;
  static std::shared_ptr<PlatformServices> services = platform_services
    ? std::move(platform_services)
    : std::make_shared<PlatformServices>();
  static PlatformServices::ModuleHandle origin_module_handle = nullptr;
  if (origin_module_handle == nullptr) {
    origin_module_handle
      = services->GetModuleHandleFromReturnAddress(oxygen::ReturnAddress<>());
  }
  static auto instance = std::unique_ptr<GraphicsBackendLoader>(
    new GraphicsBackendLoader(origin_module_handle, services));

  // Allow to reset the loader by calling it again with a platform services
  // instances (mainly for testing purposes), but only from the main module
  // again.
  if (!first_call && platform_services) {
    try {
      EnforceMainModuleRestriction(
        platform_services, "GetInstance", oxygen::ReturnAddress<>());
    } catch (const loader::InvalidOperationError&) {
      LOG_F(ERROR,
        "Resetting the platform services must be made from the main executable "
        "module");
      throw;
    }
    LOG_F(INFO, "Resetting GraphicsBackendLoader with new platform services");
    services = std::move(platform_services);
    origin_module_handle
      = services->GetModuleHandleFromReturnAddress(oxygen::ReturnAddress<>());
    instance = std::unique_ptr<GraphicsBackendLoader>(
      new GraphicsBackendLoader(origin_module_handle, services));
  }

  DCHECK_NOTNULL_F(services);

  if (first_call) {
    try {
      EnforceMainModuleRestriction(
        services, "GetInstance", oxygen::ReturnAddress<>());
    } catch (const loader::InvalidOperationError&) {
      LOG_F(ERROR,
        "First call to GraphicsBackendLoader::GetInstance() must be made from "
        "the main executable module");
      instance.reset();
      services.reset();
      throw;
    }
    first_call = false;
    g_loader_init_mode = LoaderInitMode::kStrict;
  }

  return *instance;
}

/*!
 Gets the singleton instance of the graphics backend loader with relaxed
 initialization rules. Unlike `GetInstance`, which enforces that the first call
 must originate from the main executable module, this variant allows the first
 call to come from ANY module (e.g., a plugin / dynamically loaded module).

 Semantics:
  - First call: Accepted from any module; the originating module handle is
    recorded.
  - Subsequent calls: Must originate from the SAME module; otherwise an
    `InvalidOperationError` is thrown.
  - Reset behavior: Passing a non-null `platform_services` after the first
    call replaces the internal services & instance, but only if the caller
    module matches the original initializer.

 Rationale: Some integration scenarios (such as tests or tools launched via a
 plugin) may need to bootstrap the graphics loader from a module that is not
 the process main module, while still preventing multiple plugin / host copies
 from racing to reinitialize the singleton.

 @param platform_services Optional custom platform services implementation
        used for (re)initialization.
 @return Reference to the singleton instance.
 @throw loader::InvalidOperationError if a subsequent call originates from a
        different module than the first caller, or if the caller module cannot
        be resolved.

 ### Performance Characteristics

 - Time Complexity: O(1) (static local initialization + single module handle
   comparison).
 - Memory: Same as `GetInstance` (single static instance + platform services).
 - Optimization: Avoids main-module enforcement, using lightweight module
   handle comparison after first call.

 ### Usage Examples

 ```cpp
 // Initialize from a plugin module
 auto& loader = GraphicsBackendLoader::GetInstanceRelaxed();

 // Later (same module) reset for testing
 auto mock_services = std::make_shared<MockPlatformServices>();
 auto& reset_loader = GraphicsBackendLoader::GetInstanceRelaxed(mock_services);
 (void)reset_loader;
 ```

 @note Use this only when main-module-first semantics are not viable. For most
       application code, prefer `GetInstance`.
*/
auto GraphicsBackendLoader::GetInstanceRelaxed(
  std::shared_ptr<PlatformServices> platform_services) -> GraphicsBackendLoader&
{
  // Relaxed semantics:
  //  - First call may originate from ANY module (record that module handle)
  //  - Subsequent calls (including resets) must originate from the SAME
  //    module. If not, throw InvalidOperationError.
  //  - Allows injecting new platform services only from the original module.

  if (g_loader_init_mode == LoaderInitMode::kStrict) {
    LOG_F(ERROR,
      "GraphicsBackendLoader already initialized in strict mode; cannot call "
      "GetInstanceRelaxed afterwards");
    throw loader::InvalidOperationError(
      "GetInstanceRelaxed called after GetInstance (strict) initialization");
  }

  static bool first_call = true;
  static PlatformServices::ModuleHandle origin_module_handle = nullptr;
  static std::shared_ptr<PlatformServices> services = platform_services
    ? std::move(platform_services)
    : std::make_shared<PlatformServices>();
  static auto instance = std::unique_ptr<GraphicsBackendLoader>(
    new GraphicsBackendLoader(origin_module_handle, services));

  // Determine caller module (may differ from services if a different
  // platform_services was passed on subsequent calls before validation).
  PlatformServices::ModuleHandle caller_module = nullptr;
  try {
    // We purposefully do NOT enforce main module restriction here; instead we
    // only capture the module handle.
    caller_module
      = services->GetModuleHandleFromReturnAddress(oxygen::ReturnAddress<>());
  } catch (...) {
    // If we cannot resolve the caller module, treat as error.
    throw loader::InvalidOperationError(
      "Unable to resolve caller module in GetInstanceRelaxed");
  }

  if (first_call) {
    origin_module_handle = caller_module;
    first_call = false;
    g_loader_init_mode = LoaderInitMode::kRelaxed;
    // Re-create instance now that we have a concrete origin module handle.
    instance = std::unique_ptr<GraphicsBackendLoader>(
      new GraphicsBackendLoader(origin_module_handle, services));
  } else {
    if (caller_module != origin_module_handle) {
      LOG_F(ERROR,
        "GraphicsBackendLoader::GetInstanceRelaxed() called from a different "
        "module than the original initializer");
      throw loader::InvalidOperationError(
        "GetInstanceRelaxed called from different module");
    }
  }

  // Reset with new platform services only if caller module is origin.
  if (!first_call && platform_services) {
    LOG_F(INFO,
      "Resetting GraphicsBackendLoader (relaxed) with new platform services");
    services = std::move(platform_services);
    instance = std::unique_ptr<GraphicsBackendLoader>(
      new GraphicsBackendLoader(origin_module_handle, services));
  }

  DCHECK_NOTNULL_F(services);
  return *instance;
}

/*!
 Private constructor that initializes the GraphicsBackendLoader with the
 specified platform services. Creates the internal implementation (pimpl) with
 the provided services.

 @param platform_services The platform services implementation to use for module
                          loading and management.
*/
GraphicsBackendLoader::GraphicsBackendLoader(void* origin_module,
  std::shared_ptr<loader::detail::PlatformServices> platform_services)
  : pimpl_(std::make_unique<Impl>(
      static_cast<loader::detail::PlatformServices::ModuleHandle>(
        origin_module),
      std::move(platform_services)))
{
}

/*!
 Destructor that properly cleans up the graphics backend loader. The pimpl
 destructor will automatically unload any loaded backend and release all
 resources.
*/
GraphicsBackendLoader::~GraphicsBackendLoader() = default;

/*!
 Loads the specified graphics backend from a dynamically loadable module and
 constructs an instance using the provided configuration. In strict
 initialization mode (created via `GetInstance`), this method enforces the main
 module restriction; in relaxed mode (`GetInstanceRelaxed`) the enforcement is
 skipped. Only one backend instance is loaded at a time.

 @param backend The type of graphics backend to load (Direct3D12,...).
 @param config The configuration to use for initializing the backend.
 @return A weak pointer to the loaded graphics backend. If the backend could not
         be loaded, the returned pointer will be empty. If at any point the
         backend is unloaded, the returned pointer will expire and become
         unusable.
 @throw std::runtime_error if the backend module could not be loaded or
                           initialized.
 @throw InvalidOperationError if called from a non-main module.

 ### Performance Characteristics

 - Time Complexity: O(n) where n is module loading time plus dependencies
 - Memory: Allocates backend instance and module resources
 - Optimization: Reuses already-loaded modules, caches backend instances

 ### Usage Examples

 ```cpp
 auto& loader = GraphicsBackendLoader::GetInstance();
 GraphicsConfig config{};
 config.enable_debug = true;

 auto backend = loader.LoadBackend(BackendType::kDirect3D12, config);
 if (auto graphics = backend.lock()) {
     // Use the graphics backend
 }
 ```

 @note Only one backend instance can be loaded at a time. Subsequent calls will
       return the existing instance if already loaded.
*/
auto GraphicsBackendLoader::LoadBackend(const BackendType backend,
  const GraphicsConfig& config) const -> std::weak_ptr<Graphics>
{
  if (g_loader_init_mode == LoaderInitMode::kStrict) {
    EnforceMainModuleRestriction(
      pimpl_->GetPlatformServices(), "LoadBackend", oxygen::ReturnAddress<>());
  }
  return pimpl_->LoadBackend(backend, config);
}

/*!
 Unloads the currently loaded graphics backend, destroying its instance and
 rendering all weak pointers to it unusable. The module's reference count is
 decremented, and if it is no longer referenced, it is automatically unloaded.
 In strict initialization mode (see `GetInstance`) main module restriction is
 enforced; in relaxed mode it is skipped. All exceptions are swallowed to
 preserve the noexcept guarantee.

 ### Performance Characteristics

 - Time Complexity: O(1) for the call, O(n) if module destructors run
 - Memory: Releases backend instance and may free module memory
 - Optimization: Uses reference counting to avoid premature module unloading

 ### Usage Examples

 ```cpp
 auto& loader = GraphicsBackendLoader::GetInstance();
 loader.UnloadBackend(); // Safe to call even if no backend is loaded
 ```

 @note This method is noexcept and will silently handle any errors that occur
       during unloading, logging them appropriately.
*/
auto GraphicsBackendLoader::UnloadBackend() const noexcept -> void
{
  try {
    if (g_loader_init_mode == LoaderInitMode::kStrict) {
      EnforceMainModuleRestriction(pimpl_->GetPlatformServices(),
        "UnloadBackend", oxygen::ReturnAddress<>());
    }
    pimpl_->UnloadBackend();
  } catch (...) {
    // Catch any other exceptions to prevent them from propagating
    (void)0;
  }
}

/*!
 Gets the backend instance if one is currently loaded. Returns a weak pointer to
 allow safe access to the backend while preventing circular dependencies and
 allowing the backend to be unloaded independently.

 @return A weak pointer to the currently loaded graphics backend, or an empty
         pointer if no backend is loaded. The pointer will expire if the backend
         is unloaded at a later time.

 ### Performance Characteristics

 - Time Complexity: O(1) - direct pointer access
 - Memory: No allocation
 - Optimization: Returns weak_ptr for safe lifetime management

 ### Usage Examples

 ```cpp
 auto& loader = GraphicsBackendLoader::GetInstance();
 auto backend = loader.GetBackend();

 if (auto graphics = backend.lock()) {
     // Backend is available, use it
     graphics->Present();
 } else {
     // No backend loaded or backend was unloaded
 }
 ```

 @note This method does not enforce main module restriction since it's a
       read-only operation that doesn't affect singleton state.
*/
auto GraphicsBackendLoader::GetBackend() const noexcept
  -> std::weak_ptr<Graphics>
{
  return pimpl_->GetBackend();
}

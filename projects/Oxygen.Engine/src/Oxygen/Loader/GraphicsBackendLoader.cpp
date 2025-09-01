//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string>

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
#include <exception>
#include <memory>
#include <type_traits>

using oxygen::GraphicsBackendLoader;
using oxygen::GraphicsConfig;
using oxygen::SerializedBackendConfig;
using oxygen::graphics::BackendType;
using oxygen::graphics::GetGraphicsModuleApiFunc;
using oxygen::graphics::GraphicsModuleApi;
using oxygen::graphics::kGetGraphicsModuleApi;
using oxygen::loader::detail::PlatformServices;

namespace {

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
  explicit Impl(std::shared_ptr<PlatformServices> services = nullptr)
    : platform_services(
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
        const auto full_path
          = platform_services->GetExecutableDirectory() + module_name;

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
      // NB: Do not close the module here as it may still be required
      // until the exception handling frames are complete. The module, if
      // opened, will be reused for subsequent calls to `LoadBackend` or
      // will be unloaded if a call to `UnloadBackend` is made, or when
      // the loader is destroyed.
      throw;
    }
  }

  void UnloadBackend() noexcept
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

  [[nodiscard]] auto GetBackend() noexcept -> std::weak_ptr<oxygen::Graphics>
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
  void CreateBackendInstance(GraphicsModuleApi* backend_api,
    BackendType backend_type, const GraphicsConfig& config)
  {
    if (!backend_instance) {
      // Create the JSON configuration
      std::string configJson = SerializeConfigToJson(config, backend_type);

      // Create the configuration struct
      SerializedBackendConfig serializedConfig {};
      serializedConfig.json_data = configJson.c_str();
      serializedConfig.size = configJson.length();

      // Call the backend create function with the configuration
      void* instance = backend_api->CreateBackend(serializedConfig);

      if (instance == nullptr) {
        throw std::runtime_error("Failed to create backend instance");
      }

      // Store the instance with a custom deleter that will call the destroy
      // function
      backend_instance = std::shared_ptr<oxygen::Graphics>(
        static_cast<oxygen::Graphics*>(instance),
        [destroyFunc = backend_api->DestroyBackend](
          const oxygen::Graphics* instance) {
          if (instance != nullptr) {
            destroyFunc();
          }
        });
    }
  }

  // Member variables
  std::shared_ptr<oxygen::Graphics> backend_instance;
  PlatformServices::ModuleHandle backend_module { nullptr };
  std::shared_ptr<PlatformServices> platform_services;
};

namespace {

inline void EnforceMainModuleRestriction(
  const std::shared_ptr<PlatformServices>& platform_services,
  const char* functionName, void* returnAddress)
{
  PlatformServices::ModuleHandle moduleHandle
    = platform_services->GetModuleHandleFromReturnAddress(returnAddress);

  if (!platform_services->IsMainExecutableModule(moduleHandle)) {
    throw oxygen::loader::InvalidOperationError(
      fmt::format("Function `{}` called from non-main module", functionName));
  }
}

} // namespace

// Singleton implementation with injected services
auto GraphicsBackendLoader::GetInstance(
  std::shared_ptr<loader::detail::PlatformServices> platform_services)
  -> GraphicsBackendLoader&
{
  static bool first_call = true;
  static std::shared_ptr<PlatformServices> services = platform_services
    ? std::move(platform_services)
    : std::make_shared<PlatformServices>();
  static auto instance = std::unique_ptr<GraphicsBackendLoader>(
    new GraphicsBackendLoader(services));

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
    instance = std::unique_ptr<GraphicsBackendLoader>(
      new GraphicsBackendLoader(services));
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
  }

  return *instance;
}

GraphicsBackendLoader::GraphicsBackendLoader(
  std::shared_ptr<loader::detail::PlatformServices> platform_services)
  : pimpl_(std::make_unique<Impl>(std::move(platform_services)))
{
}

GraphicsBackendLoader::~GraphicsBackendLoader() = default;

auto GraphicsBackendLoader::LoadBackend(const graphics::BackendType backend,
  const GraphicsConfig& config) const -> std::weak_ptr<Graphics>
{
  EnforceMainModuleRestriction(
    pimpl_->GetPlatformServices(), "LoadBackend", oxygen::ReturnAddress<>());
  return pimpl_->LoadBackend(backend, config);
}

void GraphicsBackendLoader::UnloadBackend() const noexcept
{
  try {
    EnforceMainModuleRestriction(pimpl_->GetPlatformServices(), "UnloadBackend",
      oxygen::ReturnAddress<>());
    pimpl_->UnloadBackend();
  } catch (...) {
    // Catch any other exceptions to prevent them from propagating
    (void)0;
  }
}

auto GraphicsBackendLoader::GetBackend() const noexcept
  -> std::weak_ptr<Graphics>
{
  return pimpl_->GetBackend();
}

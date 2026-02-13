# Graphics Backend Loader

The Graphics Backend Loader is a singleton responsible for dynamically loading
and instantiating a concrete graphics backend implementation (e.g. Direct3D12,
Headless). It supports two mutually exclusive initialization modes—`strict`
and `relaxed`—to accommodate both engine runtime and editor / tooling use
cases.

---

## Initialization Modes

### Strict Mode (`GetInstance`)

Use when the first access is guaranteed to originate from the main executable
module. Enforcement:

* First call must come from the main executable module (checked via return
  address → module handle).
* Subsequent optional resets (injecting custom `PlatformServices`) must also
  originate from the main module.
* Loader methods (`LoadBackend`, `UnloadBackend`) enforce the main-module
  restriction.

### Relaxed Mode (`GetInstanceRelaxed`)

Use when initialization may legitimately occur from a non-executable module
(e.g. an editor interop DLL). Behavior:

* First call may come from any module; its module handle becomes the
  "origin module".
* All subsequent calls (including resets) must come from the same module; any
  mismatch throws `loader::InvalidOperationError`.
* Main-module enforcement is skipped in this mode.
* Backend DLL search base prefers the origin module's directory, falling back
  to the executable directory if the origin cannot be resolved.

### Mutual Exclusivity

Whichever mode is used first locks the loader. Calling the other accessor
after initialization throws `loader::InvalidOperationError`.

### Choosing a Mode

| Scenario | Recommended Mode |
|----------|------------------|
| Game / shipped runtime | Strict |
| Unit tests needing controlled injection | Strict (with platform override) |
| Editor hosting through a plugin / bridge DLL | Relaxed |
| Dynamic tool loaded into process post-start | Relaxed |

---

## Backend Module Resolution

When loading a backend, the loader constructs a backend DLL name (e.g.
`Oxygen.Graphics.Direct3D12.dll`, debug builds receive a `-d` suffix). The
full path is resolved as follows:

1. If in relaxed mode and an origin module directory was captured: use that
   directory as base.
2. Otherwise use the executable directory returned by `PlatformServices`.

If the DLL cannot be located or loaded, a `std::runtime_error` is thrown.

---

## Configuration Data

When an application initializes the Oxygen graphics system, it first constructs
a configuration data structure (`config data`) that describes its graphics
requirements—such as debug options, device preferences, and backend-specific
settings. This `config data` is then handed off to the loader, which is
responsible for dynamically selecting and loading the appropriate graphics
backend (such as Direct3D12 or Vulkan). Before passing the configuration to the
backend, the loader serializes the `config data` into a JSON string, producing a
`serialized config data` object. This serialized form is then passed across the
module boundary to the backend.

Serializing the configuration to JSON provides several key benefits. Most
importantly, it avoids the issues associated with passing STL types (like
`std::string` or `std::optional`) across DLL boundaries, which can lead to ABI
incompatibilities and runtime errors due to differences in compiler settings or
standard library implementations. By using a plain JSON string (with a pointer
and size), the loader ensures safe and reliable communication between the
application, loader, and backend, regardless of their individual build
environments.

Additionally, JSON is a widely supported, human-readable format, making it easy
to inspect, debug, and extend configuration data. This approach decouples the
application and backend implementations, enabling flexible, robust, and
forward-compatible graphics system initialization.

Below is an example of how the config data is setup on the application side:

```cpp
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

oxygen::GraphicsConfig config{};
config.enable_debug = true;
config.enable_validation = true;
config.enable_imgui = true;           // Example additional flag
config.enable_vsync = true;           // VSync preference
config.preferred_card_name = "NVIDIA";
config.preferred_card_device_id = 123456789;
config.headless = false;
config.extra = R"({"custom_option": 42, "shader_cache": true})"; // Backend-specific JSON

// Strict mode (engine runtime)
auto& loader_strict = oxygen::GraphicsBackendLoader::GetInstance();
oxygen::PathFinderConfig path_finder_config {};
auto graphics_strict = loader_strict.LoadBackend(
  oxygen::graphics::BackendType::kDirect3D12, config, path_finder_config);

// Relaxed mode (e.g., editor plugin) – call only if strict not already used
// auto& loader_relaxed = oxygen::GraphicsBackendLoader::GetInstanceRelaxed();
// auto graphics_relaxed = loader_relaxed.LoadBackend(
//   oxygen::graphics::BackendType::kDirect3D12, config, path_finder_config);
```

When this data is passed to the Loader, it gets serialized into a JSON format,
producing an equivalent string as following:

```json
{
  "backend_type": "Direct3D12",
  "enable_debug": true,
  "enable_validation": true,
  "headless": false,
  "enable_imgui": true,
  "enable_vsync": true,
  "preferred_card_name": "NVIDIA",
  "preferred_card_device_id": 123456789,
  "custom_option": 42,
  "shader_cache": true
}
```

That serialized config data is then passed to the dynamically loaded backend
using the `CreateBackend` entry point in the loaded module. Implementation of
that entry point should parse the serialized string and use it as appropriate to
setup the graphics backend:

```cpp
void* CreateBackend(const SerializedBackendConfig& config,
                    const SerializedPathFinderConfig& path_finder_config)
{
  std::string json(config.json_data, config.size);
  auto parsed = nlohmann::json::parse(json);
  std::string paths_json(path_finder_config.json_data, path_finder_config.size);
  auto parsed_paths = nlohmann::json::parse(paths_json);
  // Example: read settings
  const bool enable_debug = parsed.value("enable_debug", false);
  const std::string backend = parsed.value("backend_type", "");
  const std::string workspace_root
    = parsed_paths.value("workspace_root_path", "");
  // ... construct and return backend implementation instance ...
}
```

---

## Error Handling Summary

| Condition | Exception / Result |
|-----------|--------------------|
| Strict mode first call not from main module | `loader::InvalidOperationError` |
| Calling other mode after initialization | `loader::InvalidOperationError` |
| Relaxed subsequent call from different module | `loader::InvalidOperationError` |
| Backend DLL load failure | `std::runtime_error` |
| Symbol resolution failure | `std::runtime_error` |

---

## Testing & Resets

Both modes allow injecting a custom `PlatformServices` instance on subsequent
calls for test isolation, subject to their mode-specific module origin rules.
Resetting recreates the internal implementation and discards any previously
loaded backend instance.

---

## Future Improvements (Potential)

* Cross-platform implementations of `GetModuleDirectory`.
* Optional query API to introspect current loader mode.
* More granular diagnostics / tracing hooks.

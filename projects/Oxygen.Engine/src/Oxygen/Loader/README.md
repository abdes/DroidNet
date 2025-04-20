# Graphics Backend Loader

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

oxygen::GraphicsConfig config;
config.enable_debug = true;
config.enable_validation = true;
config.preferred_card_name = "NVIDIA";
config.preferred_card_device_id = 123456789;
config.headless = false;
config.extra = R"({"custom_option": 42, "vsync": true})"; // Backend-specific JSON

// Load the backend (e.g., Direct3D12)
auto graphics = oxygen::GraphicsBackendLoader::Instance().LoadBackend(
    oxygen::graphics::BackendType::Direct3D12, config
);
```

When this data is passed to the Loader, it gets serialized into a JSON format, producing an equivalent string as following:

```json
{
  "enable_debug": true,
  "enable_validation": true,
  "preferred_card_name": "NVIDIA",
  "preferred_card_device_id": 123456789,
  "headless": false,
  "custom_option": 42,
  "vsync": true
}
```

That serialized config data is then passed to the dynamically loaded backend using the `CreateBackend` entry point in the loaded module. Implementation of that entry point should parse the serialized string and use it as appropriate to setup the graphics backend:

```cpp
void* CreateBackend(const SerializedBackendConfig& config) {
    std::string json(config.json_data, config.size);
    auto parsed = nlohmann::json::parse(json);
    // Use parsed configuration for initialization...
}
```

# Truly Global Objects Across DLLs

This document explains how the Oxygen Engine ensures truly global singleton
instances across different module boundaries (executable, static libraries, and
dynamic libraries) in both static and shared builds.

## The Problem

In C++, static storage duration variables (including singletons) are typically
scoped to their compilation unit or library. This creates several challenges
when working with multiple modules:

1. **Module Isolation**: Each DLL/executable maintains its own copy of static
   variables
2. **Initialization Order**: DLL static variables initialize before executable
   static variables
3. **Runtime Loading**: DLLs loaded at runtime may create separate singleton
   instances
4. **Mixed Linking**: Combining static and dynamic linking can create multiple
   instances

Without a cross-module singleton mechanism, you could end up with multiple
`TypeRegistry` instances across different modules, breaking the fundamental
assumption of singleton behavior.

## The Solution: Cross-Module Symbol Lookup

The `GetTrulySingleInstance<T>()` function implements a cross-module singleton
pattern that ensures only one instance exists per process, regardless of how
modules are linked or loaded.

### Core Algorithm

1. **Search for existing instance**: Walk through all loaded modules looking for
   a specially-named initialization function
2. **Use existing instance**: If found, call the function to get the shared
   instance
3. **Create new instance**: If not found, create a new instance using OS
   allocator
4. **Register for sharing**: The instance becomes available to subsequently
   loaded modules

### Platform-Specific Implementation

#### Windows Implementation

```cpp
// Enumerate all loaded modules (executable first, then DLLs)
EnumProcessModules(GetCurrentProcess(), modules, ...);

for (auto& module : modules) {
    // Look for "Initialize" + type_name function
    auto symbol_name = "Initialize" + type_name;
    FARPROC proc = GetProcAddress(module, symbol_name.c_str());
    if (proc) {
        auto init_func = reinterpret_cast<InstanceT* (*)()>(proc);
        instance = init_func();
        break;
    }
}
```

**Key Points:**
- `EnumProcessModules` returns the main executable as the first module
- Searches executable first, then all loaded DLLs
- Uses `GetModuleFileNameA` to identify which module provides the symbol

#### macOS Implementation

```cpp
// Enumerate all loaded images (executable at index 0)
int count = _dyld_image_count();
for (int i = 0; i < count; ++i) {
    const char* image_name = _dyld_get_image_name(i);
    void* handle = dlopen(image_name, RTLD_LAZY | RTLD_NOLOAD);

    auto symbol_name = "Initialize" + type_name;
    auto init_func = (InstanceT* (*)())dlsym(handle, symbol_name.c_str());
    if (init_func) {
        instance = init_func();
        break;
    }
}
```

**Key Points:**
- Image index 0 is always the main executable
- `RTLD_NOLOAD` prevents loading modules that aren't already loaded
- Uses `_dyld_get_image_name` to identify the providing module

#### Linux Implementation

```cpp
// First try default scope (includes main executable)
auto symbol_name = "Initialize" + type_name;
auto init_func = (InstanceT* (*)())dlsym(RTLD_DEFAULT, symbol_name.c_str());
if (init_func) {
    instance = init_func();
} else {
    // Fallback: iterate through all loaded modules
    dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void* data) {
        // Search each loaded module...
    }, &callback_data);
}
```

**Key Points:**
- `RTLD_DEFAULT` searches the main executable and all loaded libraries
- `dl_iterate_phdr` provides fallback for explicit module enumeration
- Uses `info->dlpi_name` to identify the providing module

## Memory Management Requirements

### OS Allocator Only

The shared instance **must** use the OS process-wide allocator because:

1. **Cross-module deallocation**: Instance may be deallocated by a different
   module than the one that created it
2. **Module unloading**: Creating module might unload before the instance is
   destroyed
3. **Heap compatibility**: Different modules may use different C runtime heaps

```cpp
// CORRECT: Use OS allocator
instance = new InstanceT();
std::atexit([]() { delete instance; });

// INCORRECT: Module-specific allocator
instance = std::make_unique<InstanceT>(); // May use module's heap
```

### No Virtual Functions

The shared instance **cannot** contain virtual functions because:

1. **Vtable locations**: Virtual function tables exist in specific module
   segments
2. **Module unloading**: Vtable becomes invalid if the defining module unloads
3. **ABI compatibility**: Different modules may have different vtable layouts

```cpp
// CORRECT: Plain data and non-virtual functions
class TypeRegistry {
    std::unordered_map<TypeId, TypeInfo> types_;
    void RegisterType(const TypeInfo& info) { /* ... */ }
};

// INCORRECT: Virtual functions
class TypeRegistry {
    virtual void RegisterType(const TypeInfo& info) = 0; // Dangerous!
};
```

## Usage Patterns

### Static Builds

In static builds, all code is linked into the main executable:

```cpp
// All modules share the same TypeRegistry instance
auto& registry = GetTrulySingleInstance<TypeRegistry>("TypeRegistry");
```

**Behavior:**
- Symbol lookup finds the instance in the main executable
- No cross-module issues since everything is in one module
- Standard singleton behavior applies

### Dynamic Builds (DLL/Shared Libraries)

In dynamic builds, code is distributed across multiple modules:

```cpp
// Module A (DLL)
auto& registry_a = GetTrulySingleInstance<TypeRegistry>("TypeRegistry");

// Module B (DLL)
auto& registry_b = GetTrulySingleInstance<TypeRegistry>("TypeRegistry");

// registry_a and registry_b are the SAME instance
```

**Behavior:**
- First module to call `GetTrulySingleInstance` creates the instance
- Subsequent modules find and reuse the existing instance
- True singleton behavior across all modules

### Mixed Builds (Static + Dynamic)

When combining static and dynamic linking:

```cpp
// Static library linked to executable
auto& registry_static = GetTrulySingleInstance<TypeRegistry>("TypeRegistry");

// Runtime-loaded DLL
HMODULE dll = LoadLibrary("plugin.dll");
// DLL internally calls GetTrulySingleInstance and gets the same instance
```

**Behavior:**
- The first module (executable or DLL) creates the instance
- All subsequent modules share the same instance
- Order-independent: works regardless of load order

## Symbol Naming Convention

The system looks for initialization functions with the pattern:

```
"Initialize" + TypeName
```

Examples:
- `InitializeTypeRegistry` for `TypeRegistry`
- `InitializeComponentPoolRegistry` for `ComponentPoolRegistry`
- `InitializeResourceManager` for `ResourceManager`

Each module that wants to provide a shared instance must export a function with
this exact name:

```cpp
// In some DLL or executable
extern "C" TypeRegistry* InitializeTypeRegistry() {
    static TypeRegistry instance;
    return &instance;
}
```

## Error Handling and Fallbacks

If no module exports the required initialization function:

1. **Warning message**: Displays detailed warning about the situation
2. **Local fallback**: Creates a local instance using `new`
3. **Cleanup registration**: Uses `std::atexit` for cleanup
4. **Limitation notice**: Warns that this won't work with runtime-loaded DLLs

```text
 --------------------------------------------------------------------------------
|  -*- WARNING -*- Could not find a dynamically loaded module that exports the
|    InitializeTypeRegistry function.
|  > Falling back to creating a local instance, which could work if the executable
|  > uses the type system only through static linking. It will not work if you
|  > later load a DLL that uses the type system.
|
|  > For consistent and reliable use of the type system, link to at least one DLL
|  > using it. There is always the `Oxygen.CS-Init` DLL that can fulfill that
|  > need. Ensure you call its `InitializeTypeRegistry` to force the linker to
|  > keep it.
 --------------------------------------------------------------------------------
```

## Best Practices

### For Library Authors

1. **Always provide an initialization function** in at least one DLL
2. **Export the function with C linkage** to avoid name mangling
3. **Use static local variables** for instance storage within the function
4. **Don't rely on global constructors** for cross-module singletons

### For Application Developers

1. **Link to at least one DLL** that provides the initialization function
2. **Call the initialization function explicitly** to ensure linker keeps it
3. **Avoid assumptions about initialization order** between modules
4. **Test with both static and dynamic builds** to ensure consistency

## Debugging and Verification

The system prints debug information showing which module provides the instance:

```
Found InitializeTypeRegistry in module: C:\path\to\Oxygen.Composition.dll
Found InitializeComponentPool in module: /usr/lib/libOxygen.so
```

This helps verify:
- The correct module is providing the instance
- The executable is being checked first (when applicable)
- The cross-module sharing is working as expected

## Thread Safety

The `GetTrulySingleInstance` function is thread-safe:

```cpp
static std::mutex instance_mutex;
std::lock_guard lock(instance_mutex);
```

- **Initialization**: Protected by mutex during first access
- **Subsequent access**: Returns cached pointer without locking
- **Cross-module**: Each module has its own cached pointer to the shared
  instance

However, the **instance itself** may need additional synchronization depending
on its usage patterns.

## The Oxygen::CS-Init DLL for Static Builds

### Problem with Pure Static Builds

In pure static builds where everything is linked into the main executable, the
cross-module singleton system faces a chicken-and-egg problem:

1. **No exported symbols**: Static builds don't export the `Initialize*`
   functions by default
2. **Symbol lookup fails**: `GetTrulySingleInstance` can't find the
   initialization function
3. **Fallback behavior**: System falls back to local instance creation
4. **Runtime DLL issues**: If a DLL is loaded later, it creates a separate
   instance

### The CS-Init Solution

The `Oxygen::CS-Init` DLL (Composition System Initialization) provides a
lightweight solution for static builds:

```cpp
// The CS-Init DLL exports initialization functions for core singletons
extern "C" {
    OXYGEN_CS_INIT_API TypeRegistry* InitializeTypeRegistry();
    OXYGEN_CS_INIT_API ComponentPoolRegistry* InitializeComponentPoolRegistry();
    // ... other core singletons
}
```

### How It Works

1. **Minimal DLL**: Contains only initialization functions and core singleton
   instances
2. **Always available**: Loaded alongside the main executable
3. **Cross-module bridge**: Provides the shared instances that both static and
   dynamic code can use
4. **Fallback prevention**: Prevents the system from falling back to local
   instances

### Usage in Static Builds

#### Traditional Static Build (Problematic)
```cpp
// main.exe (static build)
auto& registry = GetTrulySingleInstance<TypeRegistry>("TypeRegistry");
// WARNING: Falls back to local instance!

// Later: runtime DLL loading
HMODULE plugin = LoadLibrary("plugin.dll");
// plugin.dll gets a DIFFERENT TypeRegistry instance!
```

#### Static Build with CS-Init (Correct)
```cpp
// Link against: Oxygen.Composition.lib + Oxygen.CS-Init.lib
// Deploy: main.exe + Oxygen.CS-Init.dll

// main.exe (static build)
auto& registry = GetTrulySingleInstance<TypeRegistry>("TypeRegistry");
// SUCCESS: Finds InitializeTypeRegistry in CS-Init.dll

// Later: runtime DLL loading
HMODULE plugin = LoadLibrary("plugin.dll");
// plugin.dll gets the SAME TypeRegistry instance from CS-Init.dll
```

### Linking Instructions

#### CMake
```cmake
# For static builds, always link the CS-Init DLL
target_link_libraries(your_target
    PRIVATE
    Oxygen::Composition      # Static library
    Oxygen::CS-Init         # Initialization DLL
)
```

#### Manual Linking
```cpp
// Force linker to keep the CS-Init dependency
#pragma comment(lib, "Oxygen.CS-Init.lib")

// Optional: Explicit initialization to guarantee loading
extern "C" void EnsureCSInitLoaded();
int main() {
    EnsureCSInitLoaded();  // Ensures CS-Init.dll is loaded
    // ... rest of application
}
```

### CS-Init Implementation Details

The CS-Init DLL implements the initialization functions using static local
variables:

```cpp
// Inside Oxygen.CS-Init.dll
extern "C" OXYGEN_CS_INIT_API TypeRegistry* InitializeTypeRegistry() {
    // Thread-safe initialization (C++11 guarantees)
    static TypeRegistry instance;
    return &instance;
}

extern "C" OXYGEN_CS_INIT_API ComponentPoolRegistry* InitializeComponentPoolRegistry() {
    static ComponentPoolRegistry instance;
    return &instance;
}
```

**Key Features:**
- **Thread-safe**: C++11 guarantees thread-safe static local initialization
- **Lazy initialization**: Instances created only when first requested
- **Process-wide**: Same instance shared across all modules in the process
- **OS allocator**: Uses standard `new`/`delete` (OS allocator)

### When to Use CS-Init

| Build Type | CS-Init Needed? | Reason |
|------------|----------------|---------|
| Pure Static | **Yes** | Prevents fallback to local instances |
| Pure Dynamic | Optional | Composition DLL already provides initialization functions |
| Mixed (Static + Dynamic) | **Yes** | Ensures consistency between static and dynamic code |
| Plugin Architecture | **Yes** | Runtime-loaded plugins need shared instances |

### Deployment Considerations

#### Static Build Deployment
```
MyApplication/
├── main.exe                    # Your static application
├── Oxygen.CS-Init.dll         # Initialization DLL (required)
└── plugins/                   # Optional runtime plugins
    ├── plugin1.dll
    └── plugin2.dll
```

### Alternative: Custom Initialization DLL

For advanced scenarios, you can create your own initialization DLL:

```cpp
// MyInitDLL.cpp
#include <Oxygen/Composition/TypeRegistry.h>

extern "C" __declspec(dllexport) oxygen::TypeRegistry* InitializeTypeRegistry() {
    static oxygen::TypeRegistry instance;
    // Custom initialization
    instance.RegisterCustomTypes();
    return &instance;
}
```

This allows you to:
- **Customize initialization**: Pre-register types, configure settings
- **Control dependencies**: Ensure your initialization DLL is always present
- **Brand consistency**: Use your own DLL naming conventions

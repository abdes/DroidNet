//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Platforms.h>

#if defined(OXYGEN_WINDOWS)
// clang-format off
#  include <windows.h>
#  include <psapi.h>
#  include <array>
#  include <span>
// clang-format on
#elif defined(OXYGEN_APPLE)
#  include <dlfcn.h>
#  include <mach-o/dyld.h>
#elif defined(OXYGEN_LINUX)
#  include <dlfcn.h>
#  include <link.h>
#endif

#include <iostream>
#include <shared_mutex>

#include <Oxygen/Base/Logging.h>

namespace oxygen::composition::detail {

// We need to ensure that a single instance of the TypeRegistry exists in the
// process. This should work when the TypeRegistry is being used from a
// statically linked library, a dynamically linked DLL or a mix of both.
//
// The challenge is that, at least on Windows, static storage duration variables
// from DLLs are initialized before the main executable static storage duration
// variables. The latter are initialized as part of the CRT startup, which
// happens after DLLs are loaded. So, we cannot rely on the main executable for
// the single instance of the TypeRegistry when there is at least one DLL module
// in the process that uses it. This becomes particularly tricky when a DLL
// using the type registry is loaded at runtime.
//
// The most reliable solution is to always use a DLL for implementing the
// `InitializeTypeRegistry` function.
//
// As each module loads, it walks the list of previously loaded modules looking
// for any that export a specially-named function. If it finds one (any, doesn't
// matter which, because the invariant is that all fully-loaded modules are
// aware of the common object), it gets the address of the common object from
// the previously loaded module, then increments the reference count. If it's
// unable to find any, it allocates new data and initializes the reference
// count. During module unload, it decrements the reference count and frees the
// common object if the reference count reached zero.
//
// Of course it's necessary to use the OS allocator for the common object,
// because although unlikely, it's possible that it is de-allocated from a
// different library from the one which first loaded it. This also implies that
// the common object cannot contain any virtual functions or any other sort of
// pointer to segments of the different modules. All its resources must by
// dynamically allocated using the OS process-wide allocator.

template <typename InstanceT>
auto GetTrulySingleInstance(std::string_view type_name) -> InstanceT&
{
  static InstanceT* instance { nullptr };
  static std::mutex instance_mutex;
  // Maximum number of modules to enumerate
  constexpr size_t kMaxModules = 1024;

  std::lock_guard lock(instance_mutex);
  if (instance == nullptr) {
#if defined(OXYGEN_WINDOWS)
    std::array<HMODULE, kMaxModules> modules {};
    DWORD bytes_needed { 0 };

    if (EnumProcessModules(GetCurrentProcess(), modules.data(),
          static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
          &bytes_needed)) {
      const size_t module_count = bytes_needed / sizeof(HMODULE);
      std::span module_span { modules.begin(), module_count };

      // Check executable first (it's always the first module returned by
      // EnumProcessModules) Then check all other dynamically loaded modules
      for (const auto& module : module_span) {
        union {
          FARPROC proc;
          InstanceT* (*init)();
        } func_cast {};

        using namespace std::string_literals;
        auto symbol_name = "Initialize"s + type_name.data();
        func_cast.proc = GetProcAddress(module, symbol_name.c_str());
        if (func_cast.init != nullptr) {
          // Get module name for debugging
          std::array<char, MAX_PATH> module_name {};
          if (GetModuleFileNameA(module, module_name.data(),
                static_cast<DWORD>(module_name.size()))) {
            std::cout << "Found " << symbol_name
                      << " in module: " << module_name.data() << std::endl;
          }
          instance = func_cast.init();
          break;
        }
      }
    }
#elif defined(                                                                 \
  OXYGEN_APPLE) // Check executable first, then dynamically loaded modules
    // Image index 0 is always the main executable
    int count = _dyld_image_count();
    for (int i = 0; i < count; ++i) {
      const char* image_name = _dyld_get_image_name(i);
      void* handle = dlopen(image_name, RTLD_LAZY | RTLD_NOLOAD);
      if (handle) {
        typedef InstanceT* (*init_t)();
        using namespace std::string_literals;
        auto symbol_name = "Initialize"s + type_name.data();
        init_t init = (init_t)dlsym(handle, symbol_name.c_str());
        if (init) {
          std::cout << "Found " << symbol_name << " in module: " << image_name
                    << std::endl;
          instance = init();
          dlclose(handle);
          break;
        }
        dlclose(handle);
      }
    }
#elif defined(OXYGEN_LINUX) // Check executable first using RTLD_DEFAULT
                            // (includes main executable)
    typedef InstanceT* (*init_t)();
    using namespace std::string_literals;
    auto symbol_name = "Initialize"s + type_name.data();
    init_t init = (init_t)dlsym(RTLD_DEFAULT, symbol_name.c_str());
    if (init) {
      std::cout << "Found " << symbol_name
                << " in default scope (main executable or loaded libraries)"
                << std::endl;
      instance = init();
    } else {
      // If not found in default scope, iterate through loaded modules
      struct CallbackData {
        InstanceT** instance_ptr;
        bool found;
        std::string symbol_name;
      } callback_data { &instance, false, symbol_name };

      auto callback
        = [](struct dl_phdr_info* info, size_t /*size*/, void* data) -> int {
        auto* cb_data = static_cast<CallbackData*>(data);

        // Skip if already found
        if (cb_data->found)
          return 0;

        void* handle = dlopen(info->dlpi_name, RTLD_LAZY | RTLD_NOLOAD);
        if (handle) {
          typedef InstanceT* (*init_t)();
          init_t init = (init_t)dlsym(handle, cb_data->symbol_name.c_str());
          if (init) {
            std::cout << "Found " << cb_data->symbol_name
                      << " in module: " << info->dlpi_name << std::endl;
            *(cb_data->instance_ptr) = init();
            cb_data->found = true;
            dlclose(handle);
            return 1; // Stop iteration
          }
          dlclose(handle);
        }
        return 0; // Continue iteration
      };

      dl_iterate_phdr(callback, &callback_data);
    }
#else
    throw std::runtime_error("Unsupported platform");
#endif
  }
  if (instance == nullptr) {
    // Cannot use logging here or any fancy stuff that uses variables with
    // static storage duration, because main has not been called yet.
    std::cout << R"(
 --------------------------------------------------------------------------------
|  -*- WARNING -*- Could not find a dynamically loaded module that exports the
|    InitializeInstanceT function.
|  > Falling back to creating a local instance, which could work if the executable
|  > uses the type system only through static linking. It will not work if you
|  > later load a DLL that uses the type system.
|
|  > For consistent and reliable use of the type system, link to at least one DLL
|  > using it. There is always the `Oxygen.InstanceT` DLL that can fulfill that
|  > need. Ensure it you call its `InitializeInstanceT` to force the linker to
|  > keep it.
 --------------------------------------------------------------------------------
)";
    // NOLINTBEGIN(cppcoreguidelines-owning-memory)
    instance = new InstanceT();
    std::atexit([]() { delete instance; });
    // NOLINTEND(cppcoreguidelines-owning-memory)
  }
  return *instance;
}

} // namespace oxygen::composition::detail

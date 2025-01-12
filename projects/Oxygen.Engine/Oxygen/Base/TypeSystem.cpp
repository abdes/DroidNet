//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#endif

#include <iostream>
#include <shared_mutex>
#include <unordered_map>

#include "Oxygen/Base/TypeSystem.h"

using oxygen::TypeRegistry;

class TypeRegistry::Impl
{
 public:
  std::unordered_map<std::string, TypeId> type_map_;
  TypeId next_type_id_ = 1;
  std::shared_mutex mutex_;
};

TypeRegistry::TypeRegistry()
  : impl_(new Impl())
{
}

TypeRegistry::~TypeRegistry() { delete impl_; }

TypeRegistry& TypeRegistry::Get()
{
  /*
  The TypeRegistry has a single instance that is allocated on the main
  executable heap and provided to the shared library for use by any components
  that need to register types, in this DLL, other DLLs, or in the main
  executable itself.

  This truly ensures that there is only one instance of the TypeRegistry in the
  entire process.
  */
  static TypeRegistry* instance { nullptr };

  if (!instance) {
#if defined(_WIN32) || defined(_WIN64)
    const HMODULE module_handle = GetModuleHandle(nullptr);
    if (!module_handle) {
      throw std::runtime_error("Failed to get module handle");
    }

    union {
      FARPROC proc;
      TypeRegistry* (*init)();
    } func_cast;

    func_cast.proc = GetProcAddress(module_handle, "InitializeTypeRegistry");
    if (func_cast.init) {
      instance = func_cast.init();
    }
#elif defined(__APPLE__)
    void* handle = dlopen(nullptr, RTLD_LAZY);
    if (!handle) {
      throw std::runtime_error("Failed to get module handle");
    }

    typedef TypeRegistry* (*init_t)();
    init_t init = (init_t)dlsym(handle, "InitializeTypeRegistry");
    if (init) {
      instance = init();
    }
    dlclose(handle);
#else
    throw std::runtime_error("Unsupported platform");
#endif
  }
  if (!instance) {
    // Cannot use logging here or any fancy stuff that uses variables with
    // static storage duration, because main has not been called yet.
    std::cout
      << " --------------------------------------------------------------------------------\n"
         "|  -WARNING- Could not initialize TypeRegistry instance from the executable main\n"
         "|    module.\n"
         "|  > Falling back to creating an instance, that will be local to this DLL,\n"
         "|  > and in most cases not safe to use.\n"
         "|\n"
         "|  > Add the following function to your main executable module:\n"
         "|\n"
         "|    extern \"C\" TypeRegistry* InitializeTypeRegistry() {\n"
         "|      return &TypeRegistry::Get();\n"
         "|    }\n"
         " --------------------------------------------------------------------------------\n";
    instance = new TypeRegistry();
  }
  return *instance;
}

oxygen::TypeId TypeRegistry::RegisterType(const char* name) const
{
  if (!name || strnlen(name, 1) == 0)
    throw std::invalid_argument("cannot use `null` or empty type name to register a type");

  std::unique_lock lock(impl_->mutex_);
  if (const auto it = impl_->type_map_.find(name); it != impl_->type_map_.end()) {
    return it->second;
  }
  auto id = impl_->next_type_id_++;
  impl_->type_map_.emplace(name, id);
  return id;
}

oxygen::TypeId TypeRegistry::GetTypeId(const char* name) const
{
  std::shared_lock lock(impl_->mutex_);
  if (!impl_->type_map_.contains(name))
    throw std::invalid_argument(std::string("no type with name=`{") + name + "}` is registered");
  return impl_->type_map_.at(name);
}

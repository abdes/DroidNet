//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Platforms.h>

#if defined(OXYGEN_WINDOWS)
// clang-format off
#  include <windows.h>
#  include <psapi.h>
#  include <array>
#  include <span>
// clang-format on
#elif defined(OXYGEN_APPLE) || defined(OXYGEN_LINUX)
#  include <dlfcn.h>
#endif

#include <iostream>
#include <shared_mutex>

#include <Oxygen/Composition/Detail/FastIntMap.h>
#include <Oxygen/Composition/TypeSystem.h>

namespace {
auto GetTypeNameHash(const char* const name)
{
    return std::hash<std::string_view> {}(std::string_view(name, strlen(name)));
}
} // namespace

using oxygen::TypeRegistry;
using oxygen::composition::detail::FastIntMap;

class TypeRegistry::Impl {
public:
    using TypeKey = size_t;
    FastIntMap type_map_;
    TypeId next_type_id_ = 1;
    std::shared_mutex mutex_;
};

TypeRegistry::TypeRegistry()
    : impl_(new Impl())
{
}

TypeRegistry::~TypeRegistry() { delete impl_; }

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

auto TypeRegistry::Get() -> TypeRegistry&
{
    static TypeRegistry* instance { nullptr };
    static std::mutex instance_mutex;
    // Maximum number of modules to enumerate
    constexpr size_t kMaxModules = 1024;

    std::lock_guard lock(instance_mutex);

    if (instance == nullptr) {
#if defined(OXYGEN_WINDOWS)
        std::array<HMODULE, kMaxModules> modules {};
        DWORD bytes_needed { 0 };

        if (EnumProcessModules(GetCurrentProcess(),
                modules.data(), // Pass pointer to array data
                static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                &bytes_needed)) {
            const size_t module_count = bytes_needed / sizeof(HMODULE);
            std::span module_span { modules.begin(), module_count };

            for (const auto& module : module_span) {
                union {
                    FARPROC proc;
                    TypeRegistry* (*init)();
                } func_cast {};

                func_cast.proc = GetProcAddress(module, "InitializeTypeRegistry");
                if (func_cast.init != nullptr) {
                    instance = func_cast.init();
                    break;
                }
            }
        }
#elif defined(OXYGEN_APPLE) || defined(OXYGEN_LINUX)
        int count = _dyld_image_count();
        for (int i = 0; i < count; ++i) {
            const char* image_name = _dyld_get_image_name(i);
            void* handle = dlopen(image_name, RTLD_LAZY);
            if (handle) {
                typedef TypeRegistry* (*init_t)();
                init_t init = (init_t)dlsym(handle, "InitializeTypeRegistry");
                if (init) {
                    instance = init();
                    dlclose(handle);
                    break;
                }
                dlclose(handle);
            }
        }
#else
        throw std::runtime_error("Unsupported platform");
#endif
    }

    if (instance == nullptr) {
        // Cannot use logging here or any fancy stuff that uses variables with
        // static storage duration, because main has not been called yet.
        std::cout
            << " --------------------------------------------------------------------------------\n"
               "|  -*- WARNING -*- Could not find a dynamically loaded module that exports the\n"
               "|    InitializeTypeRegistry function.\n"
               "|  > Falling back to creating a local instance, which could work if the executable\n"
               "|  > uses the type system only through static linking. It will not work if you\n"
               "|  > later load a DLL that uses the type system.\n"
               "|\n"
               "|  > For consistent and reliable use of the type system, link to at least one DLL\n"
               "|  > using it. There is always the `Oxygen.TypeRegistry` DLL that can fulfill that\n"
               "|  > need. Ensure it you call its `InitializeTypeRegistry` to force the linker to\n"
               "|  > keep it.\n"
               " --------------------------------------------------------------------------------\n";
        // NOLINTBEGIN(cppcoreguidelines-owning-memory)
        instance = new TypeRegistry();
        std::atexit([]() { delete instance; });
        // NOLINTEND(cppcoreguidelines-owning-memory)
    }
    return *instance;
}

auto TypeRegistry::RegisterType(const char* name) const -> TypeId
{
    if ((name == nullptr) || strnlen(name, 1) == 0) {
        throw std::invalid_argument("cannot use `null` or empty type name to register a type");
    }
    const auto name_hash = GetTypeNameHash(name);

    std::unique_lock lock(impl_->mutex_);
    if (TypeId out_id { 0 }; impl_->type_map_.Get(name_hash, out_id)) {
        return out_id;
    }
    const auto id = impl_->next_type_id_++;
    impl_->type_map_.Insert(name_hash, id);
    return id;
}

auto TypeRegistry::GetTypeId(const char* name) const -> TypeId
{
    const auto name_hash = GetTypeNameHash(name);
    std::shared_lock lock(impl_->mutex_);
    TypeId out_id { 0 };
    if (!impl_->type_map_.Get(name_hash, out_id)) {
        throw std::invalid_argument(std::string("no type with name=`{") + name + "}` is registered");
    }
    return out_id;
}

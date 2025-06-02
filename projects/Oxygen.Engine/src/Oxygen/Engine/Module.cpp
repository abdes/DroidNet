//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Engine/Module.h>

using oxygen::core::Module;

Module::Module(std::string_view name, std::weak_ptr<Engine> engine)
    : engine_(std::move(engine))
{
    AddComponent<ObjectMetaData>(name);
}

Module::~Module()
{
    if (is_initialized_) {
        LOG_F(WARNING, "Module `{}` being destroyed, was not shutdown", Name());
        Shutdown();
    }
}

inline auto Module::Name() const -> std::string_view
{
    return GetComponent<ObjectMetaData>().GetName();
}

void Module::Initialize(const Graphics* gfx)
{
    if (is_initialized_) {
        LOG_F(WARNING, "Module `{}` is already initialized", Name());
        return;
    }
    LOG_F(INFO, "module `{}` initialize", Name());
    OnInitialize(gfx);
    is_initialized_ = true;
}

void Module::Shutdown() noexcept
{
    if (!is_initialized_) {
        LOG_F(WARNING, "Module `{}` being shutdown, was not initialized", Name());
        return;
    }
    LOG_F(INFO, "module `{}` shutdown", Name());
    OnShutdown();
    is_initialized_ = false;
}

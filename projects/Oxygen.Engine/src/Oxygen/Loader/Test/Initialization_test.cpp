//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>

#include <Oxygen/Loader/Detail/PlatformServices.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

namespace {

class TrackingPlatformServices final
  : public oxygen::loader::detail::PlatformServices {
public:
  auto GetModuleHandleFromReturnAddress(void* return_address) const
    -> ModuleHandle override
  {
    ++module_lookups;
    return PlatformServices::GetModuleHandleFromReturnAddress(return_address);
  }

  mutable unsigned module_lookups { 0 };
};

} // namespace

// Each mode/services combination runs in a fresh process so its first access
// exercises static initialization, without loading a graphics backend.
auto main(int argc, char** argv) -> int
{
  if (argc != 3) {
    return EXIT_FAILURE;
  }
  const std::span args { argv, static_cast<std::size_t>(argc) };
  const std::string_view mode { args[1] };
  const std::string_view services_kind { args[2] };
  if ((mode != "strict" && mode != "relaxed")
    || (services_kind != "default" && services_kind != "injected")) {
    return EXIT_FAILURE;
  }

  try {
    std::shared_ptr<TrackingPlatformServices> services;
    if (services_kind == "injected") {
      services = std::make_shared<TrackingPlatformServices>();
    }
    auto* const loader = mode == "strict"
      ? &oxygen::GraphicsBackendLoader::GetInstance(services)
      : &oxygen::GraphicsBackendLoader::GetInstanceRelaxed(services);
    auto* const repeated = mode == "strict"
      ? &oxygen::GraphicsBackendLoader::GetInstance()
      : &oxygen::GraphicsBackendLoader::GetInstanceRelaxed();
    if (loader != repeated || (services && services->module_lookups == 0)) {
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& ex) {
    std::cerr << "Loader initialization failed: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }
}

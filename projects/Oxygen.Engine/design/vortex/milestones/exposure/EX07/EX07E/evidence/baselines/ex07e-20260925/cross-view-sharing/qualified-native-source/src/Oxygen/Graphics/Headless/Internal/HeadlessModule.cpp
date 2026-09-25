//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/BackendObject.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace {
// Store the backend instance in a static shared_ptr so the module keeps it
// alive until DestroyBackend is called.
std::shared_ptr<oxygen::graphics::headless::Graphics> g_headless_instance;
}

extern "C" {

OXGN_HDLS_API auto CreateBackendImpl(
  const oxygen::SerializedBackendConfig& config,
  const oxygen::SerializedPathFinderConfig& path_finder_config) -> void*
{
  LOG_F(INFO, "Headless backend CreateBackend called");
  // Create and store the shared instance. For phase 1 we ignore config.
  if (!g_headless_instance) {
    auto* instance
      = new oxygen::graphics::headless::Graphics(config, path_finder_config);
    g_headless_instance
      = std::static_pointer_cast<oxygen::graphics::headless::Graphics>(
        oxygen::graphics::AdoptBackendObject(
          static_cast<oxygen::Graphics*>(instance),
          [](void* object) noexcept {
            delete static_cast<oxygen::Graphics*>(object);
          },
          instance->GetBackendLifetime()));
  }
  return g_headless_instance.get();
}

OXGN_HDLS_API auto DestroyBackendImpl() -> void
{
  LOG_SCOPE_F(INFO, "DestroyBackend");
  // Ensure orderly shutdown before resetting the stored shared_ptr. Any
  // external shared_ptr copies must be released by the caller to fully destroy
  // the instance.
  if (g_headless_instance) {
    g_headless_instance->Close();
  }
  g_headless_instance.reset();
}

static oxygen::graphics::GraphicsModuleApi kHeadlessApi {
  .CreateBackend = &CreateBackendImpl,
  .DestroyBackend = &DestroyBackendImpl,
};

} // extern "C"

extern "C" OXGN_HDLS_NDAPI auto GetGraphicsModuleApi() -> void*
{
  return &kHeadlessApi;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace {
// Store the backend instance in a static shared_ptr so the module keeps it
// alive until DestroyBackend is called.
static std::shared_ptr<oxygen::graphics::headless::Graphics>
  g_headless_instance;
}

extern "C" {

OXGN_HDLS_API void* CreateBackendImpl(
  const oxygen::SerializedBackendConfig& config)
{
  LOG_F(INFO, "Headless backend CreateBackend called");
  // Create and store the shared instance. For phase 1 we ignore config.
  g_headless_instance
    = std::make_shared<oxygen::graphics::headless::Graphics>(config);
  return reinterpret_cast<void*>(g_headless_instance.get());
}

OXGN_HDLS_API void DestroyBackendImpl()
{
  LOG_F(INFO, "Headless backend DestroyBackend called");
  // Reset the stored shared_ptr. Any external shared_ptr copies must be
  // released by the caller to fully destroy the instance.
  g_headless_instance.reset();
}

static oxygen::graphics::GraphicsModuleApi kHeadlessApi {
  .CreateBackend = &CreateBackendImpl,
  .DestroyBackend = &DestroyBackendImpl,
};

} // extern "C"

extern "C" OXGN_HDLS_NDAPI void* GetGraphicsModuleApi()
{
  return reinterpret_cast<void*>(&kHeadlessApi);
}

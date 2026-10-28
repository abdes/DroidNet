//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/BackendObject.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace {
// The module keeps the backend instance alive until DestroyBackend is called.
auto HeadlessInstance()
  -> std::shared_ptr<oxygen::graphics::headless::Graphics>&
{
  static std::shared_ptr<oxygen::graphics::headless::Graphics> instance;
  return instance;
}
} // namespace

extern "C" {

OXGN_HDLS_API auto CreateBackendImpl(
  const oxygen::SerializedBackendConfig& config,
  const oxygen::SerializedPathFinderConfig& path_finder_config) -> void*
{
  LOG_F(INFO, "Headless backend CreateBackend called");
  // Create and store the shared instance. For phase 1 we ignore config.
  auto& backend = HeadlessInstance();
  if (!backend) {
    auto instance = std::make_unique<oxygen::graphics::headless::Graphics>(
      config, path_finder_config);
    auto lifetime = instance->GetBackendLifetime();
    backend = std::static_pointer_cast<oxygen::graphics::headless::Graphics>(
      oxygen::graphics::AdoptBackendObject(
        std::move(instance), std::move(lifetime)));
  }
  return backend.get();
}

OXGN_HDLS_API auto DestroyBackendImpl() -> void
{
  LOG_SCOPE_F(INFO, "DestroyBackend");
  // Ensure orderly shutdown before resetting the stored shared_ptr. Any
  // external shared_ptr copies must be released by the caller to fully destroy
  // the instance.
  auto& backend = HeadlessInstance();
  if (backend) {
    backend->Close();
  }
  backend.reset();
}

//! The headless backend renders on no hardware adapter.
OXGN_HDLS_API auto EnumerateAdaptersImpl(
  void* /*context*/, oxygen::graphics::AdapterVisitorFunc /*visit*/) -> bool
{
  return true;
}

static oxygen::graphics::GraphicsModuleApi kHeadlessApi {
  .CreateBackend = &CreateBackendImpl,
  .DestroyBackend = &DestroyBackendImpl,
  .EnumerateAdapters = &EnumerateAdaptersImpl,
};

} // extern "C"

extern "C" OXGN_HDLS_NDAPI auto GetGraphicsModuleApi() -> void*
{
  return &kHeadlessApi;
}

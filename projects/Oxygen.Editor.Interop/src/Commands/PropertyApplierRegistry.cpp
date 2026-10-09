//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma managed(push, off)

#include <memory>
#include <mutex>

#include <Commands/DirectionalLightPropertyApplier.h>
#include <Commands/LocalFogVolumePropertyApplier.h>
#include <Commands/LocalLightPropertyApplier.h>
#include <Commands/NodePropertyApplier.h>
#include <Commands/OrthographicCameraPropertyApplier.h>
#include <Commands/PerspectiveCameraPropertyApplier.h>
#include <Commands/PropertyApplierRegistry.h>
#include <Commands/TransformPropertyApplier.h>

namespace oxygen::interop::module {

  auto PropertyApplierRegistry::Instance() -> PropertyApplierRegistry&
  {
    static PropertyApplierRegistry instance;
    return instance;
  }

  void PropertyApplierRegistry::Bootstrap()
  {
    static std::once_flag flag;
    std::call_once(flag, [] {
      auto& reg = Instance();
      reg.Register(std::make_unique<TransformPropertyApplier>());
      reg.Register(std::make_unique<PerspectiveCameraPropertyApplier>());
      reg.Register(std::make_unique<DirectionalLightPropertyApplier>());
      reg.Register(std::make_unique<OrthographicCameraPropertyApplier>());
      reg.Register(
        std::make_unique<LocalLightPropertyApplier<scene::PointLight>>());
      reg.Register(
        std::make_unique<LocalLightPropertyApplier<scene::SpotLight>>());
      reg.Register(std::make_unique<NodePropertyApplier>());
      reg.Register(std::make_unique<LocalFogVolumePropertyApplier>());
    });
  }

  void PropertyApplierRegistry::Register(
    std::unique_ptr<IComponentPropertyApplier> applier)
  {
    if (!applier) {
      return;
    }
    const auto id = applier->GetComponentId();
    appliers_[id] = std::move(applier);
  }

  auto PropertyApplierRegistry::Find(ComponentId id) const noexcept
    -> IComponentPropertyApplier*
  {
    const auto it = appliers_.find(id);
    return it == appliers_.end() ? nullptr : it->second.get();
  }

} // namespace oxygen::interop::module

#pragma managed(pop)

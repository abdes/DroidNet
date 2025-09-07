//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::data {
class MaterialAsset;
}

namespace oxygen::renderer::resources {

struct MaterialConstantsBufferInfo {
  std::shared_ptr<graphics::Buffer> buffer;
  uint32_t bindless_index = 0;
  std::unordered_map<uint32_t, uint32_t> handle_to_slot;
};

class MaterialBinder {
public:
  MaterialBinder(std::weak_ptr<Graphics> graphics,
    observer_ptr<engine::upload::UploadCoordinator> uploader,
    const data::MaterialAsset* default_material = nullptr);
  ~MaterialBinder();
  MaterialBinder(const MaterialBinder&) = delete;
  auto operator=(const MaterialBinder&) -> MaterialBinder& = delete;

  auto ProcessMaterials(
    const std::vector<const data::MaterialAsset*>& materials)
    -> MaterialConstantsBufferInfo;

private:
  std::weak_ptr<Graphics> graphics_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  const data::MaterialAsset* default_material_;
};

} // namespace oxygen::renderer::resources

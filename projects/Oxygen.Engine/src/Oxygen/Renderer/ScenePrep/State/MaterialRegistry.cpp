//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ScenePrep/State/MaterialRegistry.h>

#include <utility>

namespace oxygen::engine::sceneprep {

auto MaterialRegistry::RegisterMaterial(
  std::shared_ptr<const data::MaterialAsset> material) -> MaterialHandle
{
  if (!material) {
    return MaterialHandle { 0U };
  }
  const auto raw = material.get();
  if (const auto it = material_to_handle_.find(raw);
    it != material_to_handle_.end()) {
    return it->second;
  }
  const auto handle = next_handle_;
  // Ensure materials_ vector can be indexed by handle value
  if (materials_.size() <= static_cast<std::size_t>(handle.get())) {
    materials_.resize(static_cast<std::size_t>(handle.get()) + 1U);
  }
  materials_[static_cast<std::size_t>(handle.get())] = std::move(material);
  material_to_handle_.emplace(raw, handle);
  next_handle_ = MaterialHandle { handle.get() + 1U };
  return handle;
}

auto MaterialRegistry::GetHandle(const data::MaterialAsset* material) const
  -> std::optional<MaterialHandle>
{
  if (material == nullptr) {
    return std::nullopt;
  }
  if (const auto it = material_to_handle_.find(material);
    it != material_to_handle_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto MaterialRegistry::GetMaterial(MaterialHandle handle) const
  -> std::shared_ptr<const data::MaterialAsset>
{
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx < materials_.size()) {
    return materials_[idx];
  }
  return {};
}

auto MaterialRegistry::IsValidHandle(MaterialHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < materials_.size() && materials_[idx] != nullptr;
}

auto MaterialRegistry::GetRegisteredMaterialCount() const -> std::size_t
{
  return material_to_handle_.size();
}

} // namespace oxygen::engine::sceneprep

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Data/GeometryAsset.h>

namespace oxygen::scene::detail {

class MeshData final : public Component {
  OXYGEN_COMPONENT(MeshData)

public:
  MeshData(std::shared_ptr<const data::Mesh> mesh_asset)
    : mesh_asset_(std::move(mesh_asset))
  {
  }

  ~MeshData() override = default;

  OXYGEN_DEFAULT_COPYABLE(MeshData)
  OXYGEN_DEFAULT_MOVABLE(MeshData)

  //! Returns the mesh asset associated with this component.
  std::shared_ptr<const data::Mesh> GetMeshAsset() const noexcept
  {
    return mesh_asset_;
  }

  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    return std::make_unique<MeshData>(mesh_asset_);
  }

private:
  //! The mesh asset containing the geometry data.
  std::shared_ptr<const data::Mesh> mesh_asset_;
};

} // namespace oxygen::scene::detail

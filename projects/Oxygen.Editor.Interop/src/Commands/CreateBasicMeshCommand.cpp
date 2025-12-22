//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "Commands/CreateBasicMeshCommand.h"
#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module {

  void CreateBasicMeshCommand::Execute(CommandContext& context) {
    if (!context.Scene) {
      return;
    }

    auto sceneNode = context.Scene->GetNode(node_);
    if (!sceneNode || !sceneNode->IsAlive())
      return;

    // Normalize mesh_type to lower-case
    std::string type = meshType_;
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
      });

    std::optional<
      std::pair<std::vector<oxygen::data::Vertex>, std::vector<uint32_t>>>
      mesh_data;

    if (type == "cube") {
      mesh_data = oxygen::data::MakeCubeMeshAsset();
    }
    else if (type == "sphere") {
      mesh_data = oxygen::data::MakeSphereMeshAsset();
    }
    else if (type == "plane") {
      mesh_data = oxygen::data::MakePlaneMeshAsset();
    }
    else if (type == "cylinder") {
      mesh_data = oxygen::data::MakeCylinderMeshAsset();
    }
    else if (type == "cone") {
      mesh_data = oxygen::data::MakeConeMeshAsset();
    }
    else if (type == "torus") {
      mesh_data = oxygen::data::MakeTorusMeshAsset();
    }
    else if (type == "quad") {
      mesh_data = oxygen::data::MakeQuadMeshAsset();
    }
    else if (type == "arrowgizmo") {
      mesh_data = oxygen::data::MakeArrowGizmoMeshAsset();
    }

    if (!mesh_data) {
      return;
    }

    // Create a default material
    oxygen::data::pak::MaterialAssetDesc material_desc{};
    material_desc.header.asset_type = 7; // Material type
    const auto name_str = std::string("DefaultMaterial_") + type;
    const std::size_t maxn = sizeof(material_desc.header.name) - 1;
    const std::size_t n = (std::min)(maxn, name_str.size());
    std::memcpy(material_desc.header.name, name_str.c_str(), n);
    material_desc.header.name[n] = '\0';
    material_desc.material_domain =
      static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);

    // Simple deterministic color based on node name hash (if available) or type
    // We don't have node name here easily without querying, so just use type
    material_desc.base_color[0] = 0.8f;
    material_desc.base_color[1] = 0.8f;
    material_desc.base_color[2] = 0.8f;
    material_desc.base_color[3] = 1.0f;

    auto material = std::make_shared<const oxygen::data::MaterialAsset>(
      material_desc, std::vector<oxygen::data::ShaderReference>{});

    // Build mesh
    auto& [vertices, indices] = mesh_data.value();
    oxygen::data::pak::MeshViewDesc view_desc{};
    view_desc.first_vertex = 0;
    view_desc.vertex_count = static_cast<uint32_t>(vertices.size());
    view_desc.first_index = 0;
    view_desc.index_count = static_cast<uint32_t>(indices.size());

    auto mesh = oxygen::data::MeshBuilder(0, type)
      .WithVertices(vertices)
      .WithIndices(indices)
      .BeginSubMesh("default", material)
      .WithMeshView(view_desc)
      .EndSubMesh()
      .Build();

    // Create GeometryAsset
    oxygen::data::pak::GeometryAssetDesc geo_desc{};
    geo_desc.header.asset_type = 6; // Geometry type
    const std::size_t geo_n = (std::min)(maxn, type.size());
    std::memcpy(geo_desc.header.name, type.data(), geo_n);
    geo_desc.header.name[geo_n] = '\0';

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lod_meshes;
    lod_meshes.push_back(std::move(mesh));

    auto geometry = std::make_shared<oxygen::data::GeometryAsset>(
      geo_desc, std::move(lod_meshes));

    if (geometry) {
      sceneNode->GetRenderable().SetGeometry(geometry);
    }
  }

} // namespace oxygen::interop::module

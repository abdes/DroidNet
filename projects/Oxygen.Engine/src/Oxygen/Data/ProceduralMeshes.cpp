//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <numbers>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>

// ReSharper disable CppClangTidyModernizeUseDesignatedInitializers

namespace {

auto BuildMesh(std::string_view name,
  std::vector<oxygen::data::Vertex> vertices, std::vector<uint32_t> indices)
  -> std::unique_ptr<oxygen::data::Mesh>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::MeshViewDesc;

  auto mesh = MeshBuilder(0, name)
                .WithVertices(std::move(vertices))
                .WithIndices(std::move(indices))
                .BeginSubMesh("default", MaterialAsset::CreateDefault())
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = static_cast<uint32_t>(indices.size()),
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                .Build();

  return mesh;
}

using MeshDataPair
  = std::pair<std::vector<oxygen::data::Vertex>, std::vector<uint32_t>>;

auto HandleSphereMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(16u, 32u);
  if (!param_blob.empty()) {
    MemoryStream stream(std::span<std::byte>(
      const_cast<std::byte*>(param_blob.data()), param_blob.size()));
    Reader<MemoryStream> reader(stream);
    bool exhausted = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (void(std::initializer_list<int> { (
         [&] {
           if (exhausted) {
             return;
           }
           using T = std::tuple_element_t<Is, decltype(defaults)>;
           auto val = reader.Read<T>();
           if (val) {
             std::get<Is>(defaults) = val.value();
           } else {
             exhausted = true;
           }
         }(),
         0)... }),
        0);
    }(std::make_index_sequence<std::tuple_size_v<decltype(defaults)>> {});
  }
  return std::apply(oxygen::data::MakeSphereMeshAsset, defaults);
}

auto HandlePlaneMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(1u, 1u, 1.0f);
  if (!param_blob.empty()) {
    MemoryStream stream(std::span<std::byte>(
      const_cast<std::byte*>(param_blob.data()), param_blob.size()));
    Reader<MemoryStream> reader(stream);
    bool exhausted = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (void(std::initializer_list<int> { (
         [&] {
           if (exhausted) {
             return;
           }
           using T = std::tuple_element_t<Is, decltype(defaults)>;
           auto val = reader.Read<T>();
           if (val) {
             std::get<Is>(defaults) = val.value();
           } else {
             exhausted = true;
           }
         }(),
         0)... }),
        0);
    }(std::make_index_sequence<std::tuple_size_v<decltype(defaults)>> {});
  }
  return std::apply(oxygen::data::MakePlaneMeshAsset, defaults);
}

auto HandleCylinderMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(16u, 1.0f, 0.5f);
  if (!param_blob.empty()) {
    MemoryStream stream(std::span<std::byte>(
      const_cast<std::byte*>(param_blob.data()), param_blob.size()));
    Reader<MemoryStream> reader(stream);
    bool exhausted = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (void(std::initializer_list<int> { (
         [&] {
           if (exhausted) {
             return;
           }
           using T = std::tuple_element_t<Is, decltype(defaults)>;
           auto val = reader.Read<T>();
           if (val) {
             std::get<Is>(defaults) = val.value();
           } else {
             exhausted = true;
           }
         }(),
         0)... }),
        0);
    }(std::make_index_sequence<std::tuple_size_v<decltype(defaults)>> {});
  }
  return std::apply(oxygen::data::MakeCylinderMeshAsset, defaults);
}

auto HandleConeMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(16u, 1.0f, 0.5f);
  if (!param_blob.empty()) {
    MemoryStream stream(std::span<std::byte>(
      const_cast<std::byte*>(param_blob.data()), param_blob.size()));
    Reader<MemoryStream> reader(stream);
    bool exhausted = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (void(std::initializer_list<int> { (
         [&] {
           if (exhausted) {
             return;
           }
           using T = std::tuple_element_t<Is, decltype(defaults)>;
           auto val = reader.Read<T>();
           if (val) {
             std::get<Is>(defaults) = val.value();
           } else {
             exhausted = true;
           }
         }(),
         0)... }),
        0);
    }(std::make_index_sequence<std::tuple_size_v<decltype(defaults)>> {});
  }
  return std::apply(oxygen::data::MakeConeMeshAsset, defaults);
}

auto HandleTorusMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(32u, 16u, 1.0f, 0.25f);
  if (!param_blob.empty()) {
    MemoryStream stream(std::span<std::byte>(
      const_cast<std::byte*>(param_blob.data()), param_blob.size()));
    Reader<MemoryStream> reader(stream);
    bool exhausted = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (void(std::initializer_list<int> { (
         [&] {
           if (exhausted) {
             return;
           }
           using T = std::tuple_element_t<Is, decltype(defaults)>;
           auto val = reader.Read<T>();
           if (val) {
             std::get<Is>(defaults) = val.value();
           } else {
             exhausted = true;
           }
         }(),
         0)... }),
        0);
    }(std::make_index_sequence<std::tuple_size_v<decltype(defaults)>> {});
  }
  return std::apply(oxygen::data::MakeTorusMeshAsset, defaults);
}

auto HandleQuadMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(1.0f, 1.0f);
  if (!param_blob.empty()) {
    MemoryStream stream(std::span<std::byte>(
      const_cast<std::byte*>(param_blob.data()), param_blob.size()));
    Reader<MemoryStream> reader(stream);
    bool exhausted = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (void(std::initializer_list<int> { (
         [&] {
           if (exhausted) {
             return;
           }
           using T = std::tuple_element_t<Is, decltype(defaults)>;
           auto val = reader.Read<T>();
           if (val) {
             std::get<Is>(defaults) = val.value();
           } else {
             exhausted = true;
           }
         }(),
         0)... }),
        0);
    }(std::make_index_sequence<std::tuple_size_v<decltype(defaults)>> {});
  }
  return std::apply(oxygen::data::MakeQuadMeshAsset, defaults);
}

auto InvokeGenerator(std::string_view generator_id,
  std::span<const std::byte> param_blob) -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;

  std::optional<MeshDataPair> mesh_data;

  if (generator_id == "Cube") {
    mesh_data = oxygen::data::MakeCubeMeshAsset();
  } else if (generator_id == "ArrowGizmo") {
    mesh_data = oxygen::data::MakeArrowGizmoMeshAsset();
  } else if (generator_id == "Sphere") {
    mesh_data = HandleSphereMesh(param_blob);
  } else if (generator_id == "Plane") {
    mesh_data = HandlePlaneMesh(param_blob);
  } else if (generator_id == "Cylinder") {
    mesh_data = HandleCylinderMesh(param_blob);
  } else if (generator_id == "Cone") {
    mesh_data = HandleConeMesh(param_blob);
  } else if (generator_id == "Torus") {
    mesh_data = HandleTorusMesh(param_blob);
  } else if (generator_id == "Quad") {
    mesh_data = HandleQuadMesh(param_blob);
  }

  return mesh_data;
}

auto ParseGeneratorAndMeshName(std::string_view full_name)
  -> std::pair<std::string_view, std::string_view>
{
  auto slash_pos = full_name.find('/');
  if (slash_pos == std::string_view::npos || slash_pos == 0
    || slash_pos == full_name.size() - 1) {
    // Invalid format
    return { "", "" };
  }
  std::string_view generator_id = full_name.substr(0, slash_pos);
  std::string_view mesh_name = full_name.substr(slash_pos + 1);
  return { generator_id, mesh_name };
}

} // namespace

/*!
 Selects and invokes the appropriate procedural mesh generator based on the
 generator id and parameter blob, returning a mesh with the specified name.

 @param full_name Generator id and mesh name, separated by a slash (e.g.,
 "Sphere/MyMesh").
 @param param_blob Parameters for the generator; binary layout must match the
 expected struct.
 @return Shared pointer to the generated Mesh, or nullptr if the generator id,
 name, or parameters are invalid.

 ### Performance Characteristics
 - O(N) time for mesh generation (N = number of vertices).
 - Allocates vertex and index buffers.
 - Tuple-based parameter parsing and single dispatch for maintainability.

 ### Usage Example
 ```cpp
 SphereParams params{16, 32};
 auto mesh = GenerateProceduralMesh("Sphere/MySphere",
    std::as_bytes(std::span{&params, 1}));
 ```

 @warning Parameter blob must contain valid parameters for the corresponding
 generator, in the correct sequence. The blob may contain partial data, but it
 will be parsed in sequence, i.e. if the generator expects 3 parameters but only
 2 are provided, the third will be set to its default value, and the provided
 parameters must match the first 2 parameters of the generator.

 @see MeshType, MeshDesc, MakeCubeMeshAsset, MakeSphereMeshAsset, BuildMesh
*/
auto oxygen::data::GenerateMeshBuffers(std::string_view full_name,
  std::span<const std::byte> param_blob) -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;

  // Parse Generator/MeshName
  auto [generator_id, mesh_name] = ParseGeneratorAndMeshName(full_name);
  if (generator_id.empty() || mesh_name.empty()) {
    // Invalid format
    return std::nullopt;
  }

  std::optional<MeshDataPair> mesh_data;

  if (generator_id == "Cube") {
    mesh_data = MakeCubeMeshAsset();
  } else if (generator_id == "ArrowGizmo") {
    mesh_data = MakeArrowGizmoMeshAsset();
  } else if (generator_id == "Sphere") {
    mesh_data = HandleSphereMesh(param_blob);
  } else if (generator_id == "Plane") {
    mesh_data = HandlePlaneMesh(param_blob);
  } else if (generator_id == "Cylinder") {
    mesh_data = HandleCylinderMesh(param_blob);
  } else if (generator_id == "Cone") {
    mesh_data = HandleConeMesh(param_blob);
  } else if (generator_id == "Torus") {
    mesh_data = HandleTorusMesh(param_blob);
  } else if (generator_id == "Quad") {
    mesh_data = HandleQuadMesh(param_blob);
  }

  return mesh_data;
}

auto oxygen::data::GenerateMesh(std::string_view full_name,
  std::span<const std::byte> param_blob) -> std::unique_ptr<oxygen::data::Mesh>
{

  // Parse Generator/MeshName
  auto [generator_id, mesh_name] = ParseGeneratorAndMeshName(full_name);
  if (generator_id.empty() || mesh_name.empty()) {
    // Invalid format
    return nullptr;
  }

  auto mesh_data = InvokeGenerator(generator_id, param_blob);
  if (!mesh_data) {
    return nullptr;
  }
  return BuildMesh(
    mesh_name, std::move(mesh_data->first), std::move(mesh_data->second));
}

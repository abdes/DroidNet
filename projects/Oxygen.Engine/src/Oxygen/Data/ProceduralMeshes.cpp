//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <numbers>
#include <tuple>
#include <type_traits>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>

// ReSharper disable CppClangTidyModernizeUseDesignatedInitializers

namespace {

namespace recipe = oxygen::data::procedural;

auto BuildMesh(std::string_view name,
  std::vector<oxygen::data::Vertex> vertices, std::vector<uint32_t> indices)
  -> std::unique_ptr<oxygen::data::Mesh>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::geometry::MeshViewDesc;

  const auto vertex_count = static_cast<uint32_t>(vertices.size());
  const auto index_count = static_cast<uint32_t>(indices.size());
  auto mesh = MeshBuilder(0, name)
                .WithVertices(std::move(vertices))
                .WithIndices(std::move(indices))
                .BeginSubMesh("default", MaterialAsset::CreateDefault())
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = index_count,
                  .first_vertex = 0,
                  .vertex_count = vertex_count,
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
  auto defaults = std::make_tuple(
    recipe::kSphereLatitudeSegments, recipe::kSphereLongitudeSegments);
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

auto HandleCapsuleMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  // Optional parameters must be a prefix of complete uint32/float32 fields.
  constexpr auto kFieldSize = sizeof(uint32_t);
  constexpr auto kParameterSize = (2U * sizeof(uint32_t)) + (2U * sizeof(float));
  static_assert(sizeof(float) == kFieldSize);
  if (param_blob.size() > kParameterSize || param_blob.size() % kFieldSize != 0U) {
    return std::nullopt;
  }
  auto parameters = std::make_tuple(recipe::kCapsuleHemisphereSegments,
    recipe::kCapsuleRadialSegments, recipe::kCapsuleHeight, recipe::kCapsuleRadius);
  oxygen::serio::ReadOnlyMemoryStream stream(param_blob);
  oxygen::serio::Reader<oxygen::serio::ReadOnlyMemoryStream> reader(stream);
  auto remaining = param_blob.size();
  auto valid = true;
  std::apply(
    [&](auto&... fields) -> void {
      const auto read = [&](auto& field) -> void {
        if (remaining == 0U || !valid) {
          return;
        }
        const auto value = reader.Read<std::remove_reference_t<decltype(field)>>();
        if (!value) {
          valid = false;
          return;
        }
        field = *value;
        remaining -= sizeof(field);
      };
      (read(fields), ...);
    },
    parameters);
  return valid ? std::apply(oxygen::data::MakeCapsuleMeshAsset, parameters)
               : std::nullopt;
}

auto HandleIcoSphereMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(recipe::kIcoSphereSubdivisionLevel);
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
  return std::apply(oxygen::data::MakeIcoSphereMeshAsset, defaults);
}

auto HandleSubdividedCubeMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(recipe::kSubdividedCubeSegments);
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
  return std::apply(oxygen::data::MakeSubdividedCubeMeshAsset, defaults);
}

auto HandlePlaneMesh(std::span<const std::byte> param_blob)
  -> std::optional<MeshDataPair>
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  auto defaults = std::make_tuple(
    recipe::kPlaneXSegments, recipe::kPlaneZSegments, recipe::kPlaneSize);
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
  auto defaults = std::make_tuple(recipe::kCylinderSegments,
    recipe::kCylinderHeight, recipe::kCylinderRadius);
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
  auto defaults = std::make_tuple(
    recipe::kConeSegments, recipe::kConeHeight, recipe::kConeRadius);
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
  auto defaults
    = std::make_tuple(recipe::kTorusMajorSegments, recipe::kTorusMinorSegments,
      recipe::kTorusMajorRadius, recipe::kTorusMinorRadius);
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
  auto defaults = std::make_tuple(recipe::kQuadWidth, recipe::kQuadHeight);
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
  } else if (generator_id == "SubdividedCube") {
    mesh_data = HandleSubdividedCubeMesh(param_blob);
  } else if (generator_id == "ArrowGizmo") {
    mesh_data = oxygen::data::MakeArrowGizmoMeshAsset();
  } else if (generator_id == "Sphere") {
    mesh_data = HandleSphereMesh(param_blob);
  } else if (generator_id == "IcoSphere" || generator_id == "GeodesicSphere") {
    mesh_data = HandleIcoSphereMesh(param_blob);
  } else if (generator_id == "Plane") {
    mesh_data = HandlePlaneMesh(param_blob);
  } else if (generator_id == "Cylinder") {
    mesh_data = HandleCylinderMesh(param_blob);
  } else if (generator_id == "Capsule") {
    mesh_data = HandleCapsuleMesh(param_blob);
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
  const auto [generator_id, mesh_name] = ParseGeneratorAndMeshName(full_name);
  if (generator_id.empty() || mesh_name.empty()) {
    return std::nullopt;
  }
  return InvokeGenerator(generator_id, param_blob);
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

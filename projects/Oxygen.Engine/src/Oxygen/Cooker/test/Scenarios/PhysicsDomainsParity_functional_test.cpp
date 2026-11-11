//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Testing/GTest.h>

//! The PhysicsDomains example keeps every collider where its body is drawn.
/*!
 Physics ignores node scale (`BodyDesc::shape_local_scale` stays 1), so a
 collider is authored in its node's unscaled frame while the node's scale
 sizes only the drawn mesh. This test generates each body's drawn mesh from
 its geometry descriptor, the way the loader does, and compares its bounds in
 the body node's frame with the collider's (the `physics` shape meanings in
 Oxygen/Core/Constants.h).
*/

namespace {

using nlohmann::json;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::internal::GeometrySource;

//! An axis-aligned box: its centre and half extents.
struct Box final {
  glm::vec3 center { 0.0F };
  glm::vec3 half { 0.0F };
};

//! The identity of the scene the sidecar and the renderables belong to; every
//! reference they hold is relative to its folder.
constexpr auto kSceneIdentity = std::string_view {
  "/Showcase/Scenes/PhysicsDomains/PhysicsDomains.oscene"
};

//! The identity a reference written in the scene or its sidecar names.
[[nodiscard]] auto ResolveSceneReference(const std::string& text) -> std::string
{
  const auto resolved = oxygen::content::ResolveReference(kSceneIdentity, text);
  return resolved.has_value() ? *resolved : std::string {};
}

[[nodiscard]] auto ProjectDir() -> std::filesystem::path
{
  return { OXYGEN_EXAMPLES_PROJECT_DIR };
}

//! The folder of the project's `Showcase` mount.
[[nodiscard]] auto MountDir() -> std::filesystem::path
{
  return ProjectDir() / "Content";
}

[[nodiscard]] auto SceneDir() -> std::filesystem::path
{
  return MountDir() / "Scenes/PhysicsDomains";
}

//! The file of a mount path in the project's `Showcase` mount.
[[nodiscard]] auto FileOf(const std::string& mount_path)
  -> std::filesystem::path
{
  constexpr auto kMount = std::string_view { "/Showcase/" };
  return MountDir() / mount_path.substr(kMount.size());
}

[[nodiscard]] auto ReadText(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream(path, std::ios::binary);
  auto text = std::ostringstream {};
  text << stream.rdbuf();
  return text.str();
}

[[nodiscard]] auto ReadJson(const std::filesystem::path& path) -> json
{
  return json::parse(ReadText(path));
}

[[nodiscard]] auto Vec3Or(
  const json& object, const char* key, const glm::vec3 fallback) -> glm::vec3
{
  if (!object.contains(key)) {
    return fallback;
  }
  const auto& value = object.at(key);
  return { value.at(0).get<float>(), value.at(1).get<float>(),
    value.at(2).get<float>() };
}

[[nodiscard]] auto IsIdentityRotation(const json& transform) -> bool
{
  if (!transform.contains("rotation")) {
    return true;
  }
  const auto& q = transform.at("rotation");
  return q.at(0).get<float>() == 0.0F && q.at(1).get<float>() == 0.0F
    && q.at(2).get<float>() == 0.0F && q.at(3).get<float>() == 1.0F;
}

//! The manifest's source file of each cooked virtual path.
[[nodiscard]] auto SourcesByVirtualPath()
  -> std::map<std::string, std::filesystem::path>
{
  auto sources = std::map<std::string, std::filesystem::path> {};
  const auto manifest
    = ReadJson(ProjectDir() / "Manifests/PhysicsDomains.import-manifest.json");
  for (const auto& job : manifest.at("jobs")) {
    if (job.contains("virtual_path") && job.contains("source")) {
      sources.emplace(job.at("virtual_path").get<std::string>(),
        FileOf(job.at("source").get<std::string>()));
    }
  }
  return sources;
}

//! The bounds of the mesh a geometry descriptor's first LOD generates.
//! `identity` is the geometry's virtual path: a buffer mesh resolves its
//! buffers relative to it.
[[nodiscard]] auto GeneratedBounds(const std::filesystem::path& descriptor,
  const std::string& identity) -> std::optional<Box>
{
  // A buffer mesh states the bounds its buffers hold: the cook checks its
  // buffers lie inside them.
  const auto document = ReadJson(descriptor);
  const auto& first_lod = document.at("lods").at(0);
  if (first_lod.value("mesh_type", std::string {}) == "standard") {
    const auto& bounds = first_lod.at("bounds");
    const auto min = Vec3Or(bounds, "min", glm::vec3 { 0.0F });
    const auto max = Vec3Or(bounds, "max", glm::vec3 { 0.0F });
    return Box { .center = (min + max) * 0.5F, .half = (max - min) * 0.5F };
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto mounts
    = oxygen::cooker::test::SourceMountTable("Showcase", MountDir());
  const auto source = GeometrySource::FromDescriptor(
    ReadText(descriptor), descriptor, identity, *mounts, diagnostics);
  if (!source.has_value() || source->lods.empty()) {
    return std::nullopt;
  }
  const auto* procedural
    = std::get_if<GeometrySource::Procedural>(&source->lods.front().mesh);
  if (procedural == nullptr) {
    return std::nullopt;
  }
  const auto buffers = oxygen::data::GenerateMeshBuffers(
    procedural->name, procedural->parameters);
  if (!buffers.has_value() || buffers->first.empty()) {
    return std::nullopt;
  }
  auto min = buffers->first.front().position;
  auto max = min;
  for (const auto& vertex : buffers->first) {
    min = glm::min(min, vertex.position);
    max = glm::max(max, vertex.position);
  }
  return Box { .center = (min + max) * 0.5F, .half = (max - min) * 0.5F };
}

//! The smallest box holding `a` and `b`.
[[nodiscard]] auto Union(const Box& a, const Box& b) -> Box
{
  const auto min = glm::min(a.center - a.half, b.center - b.half);
  const auto max = glm::max(a.center + a.half, b.center + b.half);
  return Box { .center = (min + max) * 0.5F, .half = (max - min) * 0.5F };
}

//! The bounds of a shape part (a whole shape or a compound child) in its
//! parent's frame: its own bounds scaled by `local_scale`, placed at
//! `local_position`. A rotated part has no axis-aligned answer here.
[[nodiscard]] auto PartBounds(
  const json& part, const std::map<std::string, std::filesystem::path>& sources)
  -> std::optional<Box>
{
  const auto& rotation = part.value("local_rotation", json::array());
  const auto rotated = !rotation.empty()
    && (rotation.at(0).get<float>() != 0.0F
      || rotation.at(1).get<float>() != 0.0F
      || rotation.at(2).get<float>() != 0.0F);
  if (rotated) {
    return std::nullopt;
  }
  const auto type = part.at("shape_type").get<std::string>();
  auto own = std::optional<Box> {};
  const auto radius = part.value("radius", 0.0F);
  const auto half_height = part.value("half_height", 0.0F);
  if (type == "box") {
    own = Box { .half = Vec3Or(part, "half_extents", glm::vec3 { 0.0F }) };
  } else if (type == "sphere") {
    own = Box { .half = glm::vec3 { radius } };
  } else if (type == "capsule") {
    own = Box { .half = { radius, radius, half_height + radius } };
  } else if (type == "cylinder" || type == "cone") {
    own = Box { .half = { radius, radius, half_height } };
  } else if (type == "convex_hull" || type == "triangle_mesh") {
    // The collider is cooked from this geometry's first LOD.
    const auto geometry = sources.find(
      ResolveSceneReference(part.at("geometry_ref").get<std::string>()));
    if (geometry != sources.end()) {
      own = GeneratedBounds(geometry->second, geometry->first);
    }
  } else if (type == "height_field") {
    // Columns run along +X and rows along +Y from the origin; a hole holds
    // no height.
    const auto count = part.at("sample_count").get<float>();
    const auto& spacing = part.value("sample_spacing", json::array({ 1, 1 }));
    auto low = std::numeric_limits<float>::max();
    auto high = std::numeric_limits<float>::lowest();
    for (const auto& height : part.at("heights")) {
      if (!height.is_null()) {
        low = std::min(low, height.get<float>());
        high = std::max(high, height.get<float>());
      }
    }
    const auto max = glm::vec3 { (count - 1.0F) * spacing.at(0).get<float>(),
      (count - 1.0F) * spacing.at(1).get<float>(), high };
    const auto min = glm::vec3 { 0.0F, 0.0F, low };
    own = Box { .center = (min + max) * 0.5F, .half = (max - min) * 0.5F };
  } else if (type == "compound") {
    for (const auto& child : part.at("children")) {
      const auto bounds = PartBounds(child, sources);
      if (!bounds.has_value()) {
        return std::nullopt;
      }
      own = own.has_value() ? Union(*own, *bounds) : *bounds;
    }
  }
  if (!own.has_value()) {
    return std::nullopt;
  }
  const auto scale = Vec3Or(part, "local_scale", glm::vec3 { 1.0F });
  return Box {
    .center = (own->center * scale)
      + Vec3Or(part, "local_position", glm::vec3 { 0.0F }),
    .half = own->half * scale,
  };
}

//! A collider's bounds in its node's unscaled frame.
[[nodiscard]] auto ColliderBounds(const json& shape,
  const std::map<std::string, std::filesystem::path>& sources)
  -> std::optional<Box>
{
  return PartBounds(shape, sources);
}

//! Every rigid body, collider and character binding: node and shape.
[[nodiscard]] auto ShapeBindings()
  -> std::vector<std::pair<size_t, std::string>>
{
  auto bindings = std::vector<std::pair<size_t, std::string>> {};
  const auto sidecar
    = ReadJson(SceneDir() / "PhysicsDomains.physics-sidecar.json");
  for (const auto* group : { "rigid_bodies", "colliders", "characters" }) {
    for (const auto& binding : sidecar.at("bindings").value(group, json {})) {
      bindings.emplace_back(binding.at("node_index").get<size_t>(),
        ResolveSceneReference(binding.at("shape_ref").get<std::string>()));
    }
  }
  return bindings;
}

//! Each drawn body's mesh bounds match its collider: within 2% per axis, or,
//! on an axis where the mesh is flat, lying on one of the collider's faces.
NOLINT_TEST(PhysicsDomainsParityTest, EveryDrawnBodyMatchesItsCollider)
{
  const auto scene = ReadJson(SceneDir() / "PhysicsDomains.scene.json");
  const auto& nodes = scene.at("nodes");
  const auto sources = SourcesByVirtualPath();

  const auto bindings = ShapeBindings();
  auto bodies = std::vector<size_t> {};
  for (const auto& binding : bindings) {
    bodies.push_back(binding.first);
  }

  auto checked = size_t { 0 };
  for (const auto& [node_index, shape_ref] : bindings) {
    const auto node_name = nodes.at(node_index).at("name").get<std::string>();
    SCOPED_TRACE(node_name);

    // The mesh is drawn on the body's node (scaled by it), or on children
    // that are no bodies of their own (placed and scaled by each child's own
    // transform); several drawn meshes compare by their union.
    auto drawn = std::optional<Box> {};
    for (const auto& renderable : scene.at("renderables")) {
      const auto drawn_index = renderable.at("node").get<size_t>();
      const auto& drawn_node = nodes.at(drawn_index);
      const auto on_node = drawn_index == node_index;
      const auto on_child
        = drawn_node.value("parent", -1) == static_cast<int>(node_index)
        && std::ranges::find(bodies, drawn_index) == bodies.end();
      if (!on_node && !on_child) {
        continue;
      }
      const auto source = sources.find(ResolveSceneReference(
        renderable.at("geometry_ref").get<std::string>()));
      ASSERT_NE(source, sources.end());
      const auto mesh = GeneratedBounds(source->second, source->first);
      ASSERT_HAS_VALUE(mesh);
      const auto transform = drawn_node.value("transform", json::object());
      const auto scale = Vec3Or(transform, "scale", glm::vec3 { 1.0F });
      auto box
        = Box { .center = mesh->center * scale, .half = mesh->half * scale };
      if (on_child) {
        ASSERT_TRUE(IsIdentityRotation(transform));
        box.center += Vec3Or(transform, "translation", glm::vec3 { 0.0F });
      }
      drawn = drawn.has_value() ? Union(*drawn, box) : box;
    }
    if (!drawn.has_value()) {
      continue; // A trigger volume has nothing drawn to match.
    }

    const auto shape_source = sources.find(shape_ref);
    ASSERT_NE(shape_source, sources.end());
    const auto collider
      = ColliderBounds(ReadJson(shape_source->second), sources);
    ASSERT_HAS_VALUE(collider);

    for (auto axis = 0; axis < 3; ++axis) {
      SCOPED_TRACE(testing::Message() << "axis " << axis);
      const auto tolerance = (0.02F * collider->half[axis]) + 1.0e-4F;
      if (drawn->half[axis] < 1.0e-6F) {
        const auto offset
          = std::abs(drawn->center[axis] - collider->center[axis]);
        EXPECT_NEAR(offset, collider->half[axis], tolerance)
          << "a flat mesh lies on a collider face";
        continue;
      }
      EXPECT_NEAR(drawn->half[axis], collider->half[axis], tolerance);
      EXPECT_NEAR(drawn->center[axis], collider->center[axis], tolerance);
    }
    ++checked;
  }
  EXPECT_GT(checked, 0U);
}

} // namespace

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
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Testing/GTest.h>

//! The physics_domains example keeps every collider where its body is drawn.
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

[[nodiscard]] auto SceneDir() -> std::filesystem::path
{
  return std::filesystem::path(OXYGEN_EXAMPLE_SCENES_DIR) / "physics_domains";
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
  const auto manifest = ReadJson(SceneDir() / "import-manifest.json");
  for (const auto& job : manifest.at("jobs")) {
    if (job.contains("virtual_path") && job.contains("source")) {
      sources.emplace(job.at("virtual_path").get<std::string>(),
        SceneDir() / job.at("source").get<std::string>());
    }
  }
  return sources;
}

//! The bounds of the mesh a geometry descriptor's first LOD generates.
[[nodiscard]] auto GeneratedBounds(const std::filesystem::path& descriptor)
  -> std::optional<Box>
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto source = GeometrySource::FromDescriptor(
    ReadText(descriptor), descriptor, diagnostics);
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

//! A collider's bounds in its node's unscaled frame.
[[nodiscard]] auto ColliderBounds(const json& shape) -> std::optional<Box>
{
  if (shape.contains("local_rotation")) {
    return std::nullopt;
  }
  const auto center = Vec3Or(shape, "local_position", glm::vec3 { 0.0F });
  const auto type = shape.at("shape_type").get<std::string>();
  if (type == "box") {
    return Box {
      .center = center,
      .half = Vec3Or(shape, "half_extents", glm::vec3 { 0.0F }),
    };
  }
  const auto radius = shape.value("radius", 0.0F);
  const auto half_height = shape.value("half_height", 0.0F);
  if (type == "sphere") {
    return Box { .center = center, .half = glm::vec3 { radius } };
  }
  if (type == "capsule") {
    return Box {
      .center = center,
      .half = { radius, radius, half_height + radius },
    };
  }
  if (type == "cylinder") {
    return Box { .center = center, .half = { radius, radius, half_height } };
  }
  return std::nullopt;
}

//! Every rigid body, collider and character binding: node and shape.
[[nodiscard]] auto ShapeBindings()
  -> std::vector<std::pair<size_t, std::string>>
{
  auto bindings = std::vector<std::pair<size_t, std::string>> {};
  const auto sidecar
    = ReadJson(SceneDir() / "physics_domains.physics-sidecar.json");
  for (const auto* group : { "rigid_bodies", "colliders", "characters" }) {
    for (const auto& binding : sidecar.at("bindings").value(group, json {})) {
      bindings.emplace_back(binding.at("node_index").get<size_t>(),
        binding.at("shape_ref").get<std::string>());
    }
  }
  return bindings;
}

//! Each drawn body's mesh bounds match its collider: within 2% per axis, or,
//! on an axis where the mesh is flat, lying on one of the collider's faces.
NOLINT_TEST(PhysicsDomainsParityTest, EveryDrawnBodyMatchesItsCollider)
{
  const auto scene = ReadJson(SceneDir() / "physics_domains.scene.json");
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

    // The mesh is drawn on the body's node (scaled by it) or on one child
    // that is no body of its own (placed and scaled by the child's own
    // transform).
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
      const auto source
        = sources.find(renderable.at("geometry_ref").get<std::string>());
      ASSERT_NE(source, sources.end());
      const auto mesh = GeneratedBounds(source->second);
      ASSERT_HAS_VALUE(mesh);
      const auto transform = drawn_node.value("transform", json::object());
      const auto scale = Vec3Or(transform, "scale", glm::vec3 { 1.0F });
      auto box
        = Box { .center = mesh->center * scale, .half = mesh->half * scale };
      if (on_child) {
        ASSERT_TRUE(IsIdentityRotation(transform));
        box.center += Vec3Or(transform, "translation", glm::vec3 { 0.0F });
      }
      ASSERT_FALSE(drawn.has_value()) << "more than one drawn mesh";
      drawn = box;
    }
    if (!drawn.has_value()) {
      continue; // A trigger volume has nothing drawn to match.
    }

    const auto shape_source = sources.find(shape_ref);
    ASSERT_NE(shape_source, sources.end());
    const auto collider = ColliderBounds(ReadJson(shape_source->second));
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

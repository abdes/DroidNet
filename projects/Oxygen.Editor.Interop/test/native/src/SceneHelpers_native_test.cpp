//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <EditorModule/SceneHelpers.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::interop::module::BuildHelperIcons;
using oxygen::interop::module::BuildSelectedHelpers;
using oxygen::interop::module::BuildTriad;
using oxygen::interop::module::GizmoCamera;
using oxygen::interop::module::HelperDrag;
using oxygen::interop::module::HelperHandle;
using oxygen::interop::module::HelperHandlePosition;
using oxygen::interop::module::HelperVisual;
using oxygen::interop::module::HitTestHelperHandles;
using oxygen::interop::module::HitTestTriad;
using oxygen::interop::module::kMaxConeRadians;
using oxygen::interop::module::kMinHelperRange;
using oxygen::interop::module::kTriadAxisPixels;
using oxygen::interop::module::PickHelperIcons;
using oxygen::interop::module::SceneHelperKind;
using oxygen::interop::module::SceneHelperNode;
using oxygen::interop::module::TriadAxis;
using oxygen::interop::module::TriadCenter;
using oxygen::interop::module::UuidKey;
using oxygen::vortex::ViewOverlay;

namespace {

constexpr float kViewportSize = 800.0F;

//! Looks along +Y with Z up: X runs right and Z up on screen.
auto FrontCamera() -> GizmoCamera {
  return GizmoCamera::Create(
    glm::lookAt(glm::vec3(0.0F, -10.0F, 0.0F), glm::vec3(0.0F),
      glm::vec3(0.0F, 0.0F, 1.0F)),
    glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 100.0F),
    glm::vec2(0.0F), glm::vec2(kViewportSize));
}

//! An orthographic view down -Z with Y up: X runs right and Y up on screen.
auto TopOrthoCamera() -> GizmoCamera {
  return GizmoCamera::Create(
    glm::lookAt(glm::vec3(0.0F, 0.0F, 10.0F), glm::vec3(0.0F),
      glm::vec3(0.0F, 1.0F, 0.0F)),
    glm::ortho(-10.0F, 10.0F, -10.0F, 10.0F, 0.1F, 100.0F), glm::vec2(0.0F),
    glm::vec2(kViewportSize));
}

auto Id(const std::uint8_t value) -> UuidKey {
  UuidKey id {};
  id[0] = value;
  return id;
}

auto PixelOf(const GizmoCamera& camera, const glm::vec3& point) -> glm::vec2 {
  const auto pixel = camera.Project(point);
  Assert::IsTrue(pixel.has_value());
  return *pixel;
}

auto PointLight(const glm::vec3& position, const float range)
  -> SceneHelperNode {
  SceneHelperNode node;
  node.id = Id(1);
  node.kind = SceneHelperKind::kPointLight;
  node.position = position;
  node.range = range;
  node.selected = true;
  node.editable = true;
  return node;
}

//! A spot light at the origin shining down -Z.
auto SpotLight(const float range, const float inner_degrees,
  const float outer_degrees) -> SceneHelperNode {
  SceneHelperNode node;
  node.id = Id(2);
  node.kind = SceneHelperKind::kSpotLight;
  node.direction = { 0.0F, 0.0F, -1.0F };
  node.range = range;
  node.inner_cone = glm::radians(inner_degrees);
  node.outer_cone = glm::radians(outer_degrees);
  node.selected = true;
  node.editable = true;
  return node;
}

//! Where a spot light at the origin, shining down -Z, reaches at `degrees`
//! from its axis on the +X (or -X) side, `range` away.
auto ConePoint(const float range, const float degrees, const float side)
  -> glm::vec3 {
  const auto angle = glm::radians(degrees);
  return { side * range * std::sin(angle), 0.0F, -range * std::cos(angle) };
}

auto Drag(const GizmoCamera& camera, const SceneHelperNode& node,
  const HelperHandle handle) -> HelperDrag {
  const auto position = HelperHandlePosition(camera, node, handle);
  Assert::IsTrue(position.has_value());
  auto drag = HelperDrag::Begin(camera, node, handle, PixelOf(camera, *position));
  Assert::IsTrue(drag.has_value());
  return *drag;
}

} // namespace

namespace InteropTests {

//! Light and camera icons, selected helpers with their handles, and the
//! orientation triad.
[TestClass]
public ref class SceneHelpersTests {
public:
  [TestMethod]
  void AClickPicksTheIconUnderItAndNoOther() {
    const auto camera = FrontCamera();
    auto other = PointLight({ 3.0F, 0.0F, 0.0F }, 1.0F);
    other.id = Id(9);
    const std::vector nodes { PointLight(glm::vec3(0.0F), 1.0F), other };
    const auto center = PixelOf(camera, glm::vec3(0.0F));

    const auto hits = PickHelperIcons(camera, nodes, center - glm::vec2(3.0F),
      center + glm::vec2(4.0F), 1.0F);

    Assert::AreEqual(std::size_t { 1 }, hits.size());
    Assert::IsTrue(hits[0].id == nodes[0].id);
    Assert::AreEqual(0.0F, hits[0].center_distance);
    Assert::AreEqual(camera.DeviceDepth(glm::vec3(0.0F)), hits[0].depth, 1.0e-6F);
  }

  [TestMethod]
  void AMarqueePicksEveryIconItCoversAndNoneBehindTheEye() {
    const auto camera = FrontCamera();
    auto beside = PointLight({ 3.0F, 0.0F, 0.0F }, 1.0F);
    beside.id = Id(9);
    auto behind = PointLight({ 0.0F, -20.0F, 0.0F }, 1.0F);
    behind.id = Id(10);
    const std::vector nodes { PointLight(glm::vec3(0.0F), 1.0F), beside,
      behind };

    const auto hits = PickHelperIcons(
      camera, nodes, glm::vec2(0.0F), glm::vec2(kViewportSize), 1.0F);

    Assert::AreEqual(std::size_t { 2 }, hits.size());
    Assert::IsTrue(hits[0].id == nodes[0].id);
    Assert::IsTrue(hits[1].id == beside.id);
  }

  [TestMethod]
  void IconsDrawInTheSceneLayerForEveryNodeInFrontOfTheEye() {
    const auto camera = FrontCamera();
    SceneHelperNode camera_node;
    camera_node.kind = SceneHelperKind::kCamera;
    camera_node.position = { 2.0F, 0.0F, 0.0F };
    const std::vector nodes { PointLight(glm::vec3(0.0F), 1.0F), camera_node };

    ViewOverlay overlay;
    BuildHelperIcons(camera, nodes, 1.0F, overlay);

    Assert::IsFalse(overlay.scene.triangles.empty());
    Assert::IsFalse(overlay.scene.lines.empty());
    Assert::IsTrue(overlay.top.IsEmpty());
  }

  [TestMethod]
  void APointLightRangeFollowsThePointerAndReturnsExactly() {
    const auto camera = FrontCamera();
    const auto node = PointLight(glm::vec3(0.0F), 2.0F);
    const auto handle = HelperHandlePosition(camera, node, HelperHandle::kRange);
    Assert::IsTrue(handle.has_value());
    const auto hit
      = HitTestHelperHandles(camera, std::vector { node }, PixelOf(camera, *handle), 1.0F);
    Assert::IsTrue(hit.has_value());
    Assert::IsTrue(hit->handle == HelperHandle::kRange);

    auto drag = Drag(camera, node, HelperHandle::kRange);
    const auto start = drag.Pointer();
    Assert::IsTrue(drag.Update(camera, PixelOf(camera, { 3.0F, 0.0F, 0.0F })));
    Assert::AreEqual(3.0F, drag.Value(), 0.01F);
    Assert::AreEqual(3.0F, drag.Node().range, 0.01F);

    (void)drag.Update(camera, start);
    Assert::AreEqual(2.0F, drag.Value());
  }

  [TestMethod]
  void APointLightRangeIsGrabbedOnItsSilhouette() {
    const auto camera = FrontCamera();
    const std::vector nodes { PointLight(glm::vec3(0.0F), 2.0F) };

    const auto hit = HitTestHelperHandles(
      camera, nodes, PixelOf(camera, { 0.0F, 0.0F, 2.0F }), 1.0F);

    Assert::IsTrue(hit.has_value());
    Assert::IsTrue(hit->handle == HelperHandle::kRange);
  }

  [TestMethod]
  void RangeNeverDropsBelowItsMinimum() {
    const auto camera = FrontCamera();
    auto drag = Drag(camera, SpotLight(4.0F, 20.0F, 30.0F), HelperHandle::kRange);

    (void)drag.Update(camera, PixelOf(camera, { 0.0F, 0.0F, 3.0F }));

    Assert::AreEqual(kMinHelperRange, drag.Value());
  }

  [TestMethod]
  void ASpotLightRangeFollowsItsAxis() {
    const auto camera = FrontCamera();
    auto drag = Drag(camera, SpotLight(4.0F, 20.0F, 30.0F), HelperHandle::kRange);

    Assert::IsTrue(
      drag.Update(camera, PixelOf(camera, { 0.5F, 0.0F, -6.0F })));

    Assert::AreEqual(6.0F, drag.Value(), 0.01F);
  }

  [TestMethod]
  void TheOuterConeFollowsThePointerWithinTheInnerConeAndAHemisphere() {
    const auto camera = FrontCamera();
    const auto node = SpotLight(4.0F, 20.0F, 30.0F);
    auto drag = Drag(camera, node, HelperHandle::kOuterCone);

    (void)drag.Update(camera, PixelOf(camera, ConePoint(4.0F, 45.0F, 1.0F)));
    Assert::AreEqual(glm::radians(45.0F), drag.Value(), glm::radians(0.5F));
    Assert::AreEqual(drag.Value(), drag.Node().outer_cone, 0.0F);

    (void)drag.Update(camera, PixelOf(camera, ConePoint(4.0F, 10.0F, 1.0F)));
    Assert::AreEqual(node.inner_cone, drag.Value());

    (void)drag.Update(camera, PixelOf(camera, ConePoint(4.0F, 120.0F, 1.0F)));
    Assert::AreEqual(kMaxConeRadians, drag.Value());
  }

  [TestMethod]
  void TheInnerConeNeverExceedsTheOuterCone() {
    const auto camera = FrontCamera();
    const auto node = SpotLight(4.0F, 20.0F, 30.0F);
    auto drag = Drag(camera, node, HelperHandle::kInnerCone);

    (void)drag.Update(camera, PixelOf(camera, ConePoint(4.0F, 10.0F, -1.0F)));
    Assert::AreEqual(glm::radians(10.0F), drag.Value(), glm::radians(0.5F));

    (void)drag.Update(camera, PixelOf(camera, ConePoint(4.0F, 40.0F, -1.0F)));
    Assert::AreEqual(node.outer_cone, drag.Value());
  }

  [TestMethod]
  void ConeHandlesDragLookingDownTheAxis() {
    // Seen from above, the cone's base is a circle around the axis.
    const auto camera = GizmoCamera::Create(
      glm::lookAt(glm::vec3(0.0F, 0.0F, 10.0F), glm::vec3(0.0F),
        glm::vec3(0.0F, 1.0F, 0.0F)),
      glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 100.0F),
      glm::vec2(0.0F), glm::vec2(kViewportSize));
    auto drag = Drag(camera, SpotLight(4.0F, 20.0F, 30.0F), HelperHandle::kOuterCone);

    const auto base = 4.0F * std::cos(glm::radians(30.0F));
    (void)drag.Update(camera,
      PixelOf(camera,
        { 0.0F, base * std::tan(glm::radians(40.0F)), -base }));

    Assert::AreEqual(glm::radians(40.0F), drag.Value(), glm::radians(0.5F));
  }

  [TestMethod]
  void OnlyEditableNodesOfferHandles() {
    const auto camera = FrontCamera();
    auto node = PointLight(glm::vec3(0.0F), 2.0F);
    node.editable = false;
    const auto handle = HelperHandlePosition(camera, node, HelperHandle::kRange);

    const auto hit = HitTestHelperHandles(
      camera, std::vector { node }, PixelOf(camera, *handle), 1.0F);

    Assert::IsFalse(hit.has_value());
  }

  [TestMethod]
  void DirectionalLightsAndCamerasHaveNoHandles() {
    const auto camera = FrontCamera();
    SceneHelperNode sun;
    sun.kind = SceneHelperKind::kDirectionalLight;
    SceneHelperNode lens;
    lens.kind = SceneHelperKind::kCamera;

    Assert::IsFalse(HelperHandlePosition(camera, sun, HelperHandle::kRange).has_value());
    Assert::IsFalse(HelperDrag::Begin(camera, lens, HelperHandle::kRange,
      glm::vec2(400.0F)).has_value());
  }

  [TestMethod]
  void ALocalFogVolumeDrawsItsSphereWithoutHandles() {
    const auto camera = FrontCamera();
    SceneHelperNode fog;
    fog.id = Id(5);
    fog.kind = SceneHelperKind::kLocalFogVolume;
    fog.range = 5.0F;
    fog.selected = true;
    fog.editable = true;

    Assert::IsFalse(HelperHandlePosition(camera, fog, HelperHandle::kRange).has_value());
    ViewOverlay overlay;
    BuildSelectedHelpers(camera, std::vector { fog }, HelperVisual {}, overlay);
    Assert::IsFalse(overlay.scene.lines.empty());
    for (const auto& line : overlay.scene.lines) {
      Assert::AreEqual(5.0F, glm::length(line.start), 1.0e-3F);
    }

    ViewOverlay icons;
    BuildHelperIcons(camera, std::vector { fog }, 1.0F, icons);
    Assert::IsFalse(icons.scene.lines.empty());
  }

  [TestMethod]
  void SelectedHelpersDrawOnlyForSelectedNodes() {
    const auto camera = FrontCamera();
    auto node = SpotLight(4.0F, 20.0F, 30.0F);
    node.selected = false;

    ViewOverlay hidden;
    BuildSelectedHelpers(camera, std::vector { node }, HelperVisual {}, hidden);
    Assert::IsTrue(hidden.IsEmpty());

    node.selected = true;
    ViewOverlay shown;
    BuildSelectedHelpers(camera, std::vector { node }, HelperVisual {}, shown);
    Assert::IsFalse(shown.scene.lines.empty());
  }

  [TestMethod]
  void ACameraHelperDrawsItsViewingVolume() {
    const auto camera = FrontCamera();
    SceneHelperNode lens;
    lens.kind = SceneHelperKind::kCamera;
    lens.selected = true;
    lens.direction = { 1.0F, 0.0F, 0.0F };
    lens.frustum = { {
      { 0.1F, -0.1F, -0.05F },
      { 0.1F, 0.1F, -0.05F },
      { 0.1F, 0.1F, 0.05F },
      { 0.1F, -0.1F, 0.05F },
      { 10.0F, -10.0F, -5.0F },
      { 10.0F, 10.0F, -5.0F },
      { 10.0F, 10.0F, 5.0F },
      { 10.0F, -10.0F, 5.0F },
    } };

    ViewOverlay without;
    BuildSelectedHelpers(camera, std::vector { lens }, HelperVisual {}, without);
    Assert::IsTrue(without.IsEmpty());

    lens.has_frustum = true;
    ViewOverlay with;
    BuildSelectedHelpers(camera, std::vector { lens }, HelperVisual {}, with);
    // Twelve volume edges, four lines to the frame, its four sides.
    Assert::AreEqual(std::size_t { 20 }, with.scene.lines.size());
    Assert::AreEqual(std::size_t { 3 }, with.scene.triangles.size());
  }

  [TestMethod]
  void TheTriadLettersFollowTheViewRotation() {
    const auto camera = TopOrthoCamera();
    const auto center = TriadCenter(camera, 1.0F);

    // Looking down: X to the right, Y up the screen, Z toward the eye.
    Assert::IsTrue(TriadAxis::kX
      == HitTestTriad(camera, center + glm::vec2(kTriadAxisPixels, 0.0F), 1.0F));
    Assert::IsTrue(TriadAxis::kY
      == HitTestTriad(camera, center + glm::vec2(0.0F, -kTriadAxisPixels), 1.0F));
    Assert::IsTrue(TriadAxis::kZ == HitTestTriad(camera, center, 1.0F));
    Assert::IsTrue(TriadAxis::kNone
      == HitTestTriad(camera, center + glm::vec2(-kTriadAxisPixels, 0.0F), 1.0F));
  }

  [TestMethod]
  void TheTriadSitsInTheBottomLeftCornerAndScales() {
    const auto camera = FrontCamera();

    const auto center = TriadCenter(camera, 1.0F);
    const auto scaled = TriadCenter(camera, 2.0F);

    Assert::IsTrue(center.x < kViewportSize * 0.25F);
    Assert::IsTrue(center.y > kViewportSize * 0.75F);
    Assert::IsTrue(scaled.x > center.x && scaled.y < center.y);
    // Front view: Z points up the screen.
    Assert::IsTrue(TriadAxis::kZ
      == HitTestTriad(camera, center + glm::vec2(0.0F, -kTriadAxisPixels), 1.0F));
  }

  [TestMethod]
  void TheTriadIsLettersOnlyDrawnOverEverything() {
    ViewOverlay overlay;

    BuildTriad(FrontCamera(), TriadAxis::kX, 1.0F, overlay);

    Assert::IsTrue(overlay.scene.IsEmpty());
    Assert::IsTrue(overlay.top.triangles.empty());
    Assert::IsFalse(overlay.top.lines.empty());
  }
};

} // namespace InteropTests

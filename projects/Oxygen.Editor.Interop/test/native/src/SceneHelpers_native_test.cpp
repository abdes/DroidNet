//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneInstanceComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SceneHelperController.h>
#include <EditorModule/SceneHelpers.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::interop::module::BuildHelperIcons;
using oxygen::interop::module::BuildSelectedHelpers;
using oxygen::interop::module::BuildTriad;
using oxygen::interop::module::CollectHelperNodes;
using oxygen::interop::module::GizmoCamera;
using oxygen::interop::module::HelperDrag;
using oxygen::interop::module::HelperHandle;
using oxygen::interop::module::HelperHandlePosition;
using oxygen::interop::module::HelperNodeFilter;
using oxygen::interop::module::HelperVisual;
using oxygen::interop::module::HitTestHelperHandles;
using oxygen::interop::module::HitTestTriad;
using oxygen::interop::module::kMaxConeRadians;
using oxygen::interop::module::kMinHelperRange;
using oxygen::interop::module::kTriadAxisPixels;
using oxygen::interop::module::NodeReferenceKey;
using oxygen::interop::module::NodeRegistry;
using oxygen::interop::module::PickHelperIcons;
using oxygen::interop::module::SceneHelperKind;
using oxygen::interop::module::SceneHelperNode;
using oxygen::interop::module::TriadAxis;
using oxygen::interop::module::TriadCenter;
using oxygen::interop::module::UuidKey;
using oxygen::data::NodeId;
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

auto Ref(const std::uint8_t value) -> NodeReferenceKey {
  return { .node = Id(value), .path = {} };
}

auto MakeNodeId(const std::uint8_t seed) -> NodeId {
  auto bytes = oxygen::data::StableIdBytes {};
  bytes.fill(seed);
  return NodeId::FromBytes(bytes);
}

//! A scene whose instance root `Lamp`, registered as node 7, brought in
//! the point light `Bulb` as node ID 3, beside the owned point light `Desk`,
//! registered as node 8.
struct LampScene {
  std::shared_ptr<oxygen::scene::Scene> scene
    = std::make_shared<oxygen::scene::Scene>("helpers", 64U);
  NodeReferenceKey bulb { .node = Id(7), .path = { MakeNodeId(3) } };

  LampScene() {
    NodeRegistry::ClearAll();
    auto lamp = scene->CreateNode("Lamp");
    auto bulb_node = *scene->CreateChildNode(lamp, "Bulb");
    (void)bulb_node.AttachLight(std::make_unique<oxygen::scene::PointLight>());
    auto desk = scene->CreateNode("Desk");
    (void)desk.AttachLight(std::make_unique<oxygen::scene::PointLight>());
    auto impl = lamp.GetImpl();
    Assert::IsTrue(impl.has_value());
    impl->get().AddComponent<oxygen::scene::SceneInstanceComponent>(nullptr,
      nullptr, nullptr, std::vector<oxygen::data::SceneChange> {},
      std::unordered_map<NodeId, oxygen::scene::NodeHandle> {
        { MakeNodeId(3), bulb_node.GetHandle() } });
    NodeRegistry::Register(Id(7), lamp.GetHandle());
    NodeRegistry::Register(Id(8), desk.GetHandle());
    scene->Update();
  }

  ~LampScene() { NodeRegistry::ClearAll(); }

  LampScene(const LampScene&) = delete;
  auto operator=(const LampScene&) -> LampScene& = delete;
  LampScene(LampScene&&) = delete;
  auto operator=(LampScene&&) -> LampScene& = delete;
};

auto Find(const std::vector<SceneHelperNode>& nodes,
  const NodeReferenceKey& reference) -> const SceneHelperNode* {
  const auto found = std::ranges::find(nodes, reference, &SceneHelperNode::node);
  return found == nodes.end() ? nullptr : &*found;
}

auto PixelOf(const GizmoCamera& camera, const glm::vec3& point) -> glm::vec2 {
  const auto pixel = camera.Project(point);
  Assert::IsTrue(pixel.has_value());
  return *pixel;
}

auto PointLight(const glm::vec3& position, const float range)
  -> SceneHelperNode {
  SceneHelperNode node;
  node.node = Ref(1);
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
  node.node = Ref(2);
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
    other.node = Ref(9);
    const std::vector nodes { PointLight(glm::vec3(0.0F), 1.0F), other };
    const auto center = PixelOf(camera, glm::vec3(0.0F));

    const auto hits = PickHelperIcons(camera, nodes, center - glm::vec2(3.0F),
      center + glm::vec2(4.0F), 1.0F);

    Assert::AreEqual(std::size_t { 1 }, hits.size());
    Assert::IsTrue(hits[0].node == nodes[0].node);
    Assert::AreEqual(0.0F, hits[0].center_distance);
    Assert::AreEqual(camera.DeviceDepth(glm::vec3(0.0F)), hits[0].depth, 1.0e-6F);
  }

  [TestMethod]
  void AMarqueePicksEveryIconItCoversAndNoneBehindTheEye() {
    const auto camera = FrontCamera();
    auto beside = PointLight({ 3.0F, 0.0F, 0.0F }, 1.0F);
    beside.node = Ref(9);
    auto behind = PointLight({ 0.0F, -20.0F, 0.0F }, 1.0F);
    behind.node = Ref(10);
    const std::vector nodes { PointLight(glm::vec3(0.0F), 1.0F), beside,
      behind };

    const auto hits = PickHelperIcons(
      camera, nodes, glm::vec2(0.0F), glm::vec2(kViewportSize), 1.0F);

    Assert::AreEqual(std::size_t { 2 }, hits.size());
    Assert::IsTrue(hits[0].node == nodes[0].node);
    Assert::IsTrue(hits[1].node == beside.node);
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
  void InstancedLightsHaveHelpersNamedByTheirPath() {
    const LampScene lamp;

    const auto nodes = CollectHelperNodes(*lamp.scene,
      HelperNodeFilter { .selected = { lamp.bulb }, .active = lamp.bulb });

    Assert::AreEqual(std::size_t { 2 }, nodes.size());
    const auto* bulb = Find(nodes, lamp.bulb);
    Assert::IsTrue(bulb != nullptr);
    Assert::IsTrue(bulb->selected && bulb->active);
    // Handles write the node's own light, which an instanced node is not.
    Assert::IsFalse(bulb->editable);
  }

  [TestMethod]
  void SelectedOwnedLightsAreEditableUnlessLocked() {
    const LampScene lamp;

    const auto unlocked = CollectHelperNodes(
      *lamp.scene, HelperNodeFilter { .selected = { Ref(8) } });
    const auto locked = CollectHelperNodes(*lamp.scene,
      HelperNodeFilter { .locked = { Ref(8) }, .selected = { Ref(8) } });

    const auto* free = Find(unlocked, Ref(8));
    const auto* held = Find(locked, Ref(8));
    Assert::IsTrue(free != nullptr && held != nullptr);
    Assert::IsTrue(free->editable);
    Assert::IsFalse(held->editable);
  }

  [TestMethod]
  void AHiddenInstanceRootHidesItsInstancedHelpers() {
    const LampScene lamp;

    const auto nodes = CollectHelperNodes(
      *lamp.scene, HelperNodeFilter { .hidden = { Ref(7) } });

    Assert::AreEqual(std::size_t { 1 }, nodes.size());
    Assert::IsTrue(nodes[0].node == Ref(8));
  }

  [TestMethod]
  void AHiddenInstancedNodeHasNoHelper() {
    const LampScene lamp;

    const auto nodes = CollectHelperNodes(
      *lamp.scene, HelperNodeFilter { .hidden = { lamp.bulb } });

    Assert::AreEqual(std::size_t { 1 }, nodes.size());
    Assert::IsTrue(nodes[0].node == Ref(8));
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
    fog.node = Ref(5);
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

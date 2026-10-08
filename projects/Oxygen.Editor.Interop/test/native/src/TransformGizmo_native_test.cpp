//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vector_relational.hpp>

#include <EditorModule/TransformGizmo.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::interop::module::BuildGizmoOverlay;
using oxygen::interop::module::GizmoCamera;
using oxygen::interop::module::GizmoDrag;
using oxygen::interop::module::GizmoFrame;
using oxygen::interop::module::GizmoHandle;
using oxygen::interop::module::GizmoTargetStart;
using oxygen::interop::module::GizmoVisual;
using oxygen::interop::module::HitTestGizmo;
using oxygen::interop::module::kGizmoSizePixels;
using oxygen::interop::module::TransformSnap;
using oxygen::interop::module::TransformSpace;
using oxygen::interop::module::TransformTool;
using oxygen::vortex::ViewOverlay;

namespace {

constexpr float kViewportSize = 800.0F;

//! A perspective camera at `eye` looking at the origin.
auto MakeCamera(const glm::vec3& eye, const glm::vec3& up) -> GizmoCamera {
  return GizmoCamera::Create(glm::lookAt(eye, glm::vec3(0.0F), up),
    glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 100.0F),
    glm::vec2(0.0F), glm::vec2(kViewportSize));
}

//! Looks along +Y with Z up: X runs right and Z up on screen.
auto FrontCamera() -> GizmoCamera {
  return MakeCamera({ 0.0F, -10.0F, 0.0F }, { 0.0F, 0.0F, 1.0F });
}

//! Looks down -Z with Y up: X runs right and Y up on screen.
auto TopCamera() -> GizmoCamera {
  return MakeCamera({ 0.0F, 0.0F, 10.0F }, { 0.0F, 1.0F, 0.0F });
}

auto GizmoLength(const GizmoCamera& camera) -> float {
  return kGizmoSizePixels * camera.PixelSize(glm::vec3(0.0F));
}

auto PixelOf(const GizmoCamera& camera, const glm::vec3& point) -> glm::vec2 {
  const auto pixel = camera.Project(point);
  Assert::IsTrue(pixel.has_value());
  return *pixel;
}

auto TranslateHit(const GizmoCamera& camera, const GizmoFrame& frame,
  const glm::vec3& point) -> GizmoHandle {
  return HitTestGizmo(
    camera, TransformTool::kTranslate, frame, PixelOf(camera, point), 1.0F);
}

auto Target(const glm::mat4& parent_world, const glm::vec3& local_position)
  -> GizmoTargetStart {
  GizmoTargetStart target;
  target.local_position = local_position;
  target.parent_world = parent_world;
  target.world
    = parent_world * glm::translate(glm::mat4(1.0F), local_position);
  return target;
}

auto IsNear(const glm::vec3& a, const glm::vec3& b, const float tolerance)
  -> bool {
  return glm::all(glm::epsilonEqual(a, b, tolerance));
}

auto Snap(const bool enabled) -> TransformSnap {
  TransformSnap snap;
  snap.enabled = enabled;
  return snap;
}

auto Begin(const GizmoCamera& camera, const TransformTool tool,
  const TransformSpace space, const GizmoFrame& frame,
  const GizmoHandle handle, const glm::vec2& pointer,
  std::vector<GizmoTargetStart> targets) -> GizmoDrag {
  auto drag = GizmoDrag::Begin(
    camera, tool, space, frame, handle, pointer, 1.0F, std::move(targets));
  Assert::IsTrue(drag.has_value());
  return *drag;
}

//! Moves the pointer around a Z-ring from angle `from` to `to`, in degrees
//! from +X, in steps the drag can follow.
auto TurnAboutZ(const GizmoCamera& camera, GizmoDrag& drag, const float from,
  const float to, const TransformSnap& snap) -> void {
  const auto radius = 0.85F * GizmoLength(camera);
  const auto steps = static_cast<int>(std::ceil(std::abs(to - from) / 20.0F));
  for (int i = 1; i <= steps; ++i) {
    const auto angle
      = glm::radians(from + (to - from) * static_cast<float>(i) / steps);
    (void)drag.Update(camera,
      PixelOf(camera,
        glm::vec3(std::cos(angle), std::sin(angle), 0.0F) * radius),
      snap, false);
  }
}

} // namespace

namespace InteropTests {

//! The transform gizmo's projection, hit tests, drags, snapping and geometry.
[TestClass]
public ref class TransformGizmoTests {
public:
  [TestMethod]
  void ProjectionAndRayAgreeOnAPoint() {
    const auto camera = FrontCamera();
    const glm::vec3 point { 1.5F, 2.0F, -0.75F };

    const auto ray = camera.Ray(PixelOf(camera, point));

    const auto toward = glm::normalize(point - ray.origin);
    Assert::IsTrue(IsNear(toward, ray.direction, 1.0e-4F));
  }

  [TestMethod]
  void HitTestFindsTheHandleUnderThePointer() {
    const auto camera = FrontCamera();
    const GizmoFrame frame;
    const auto length = GizmoLength(camera);
    Assert::IsTrue(TranslateHit(camera, frame, { 0.6F * length, 0.0F, 0.0F })
      == GizmoHandle::kX);
    Assert::IsTrue(TranslateHit(camera, frame, { 0.0F, 0.0F, 0.6F * length })
      == GizmoHandle::kZ);
    Assert::IsTrue(
      TranslateHit(camera, frame, { 0.38F * length, 0.0F, 0.38F * length })
      == GizmoHandle::kXZ);
    Assert::IsTrue(
      TranslateHit(camera, frame, glm::vec3(0.0F)) == GizmoHandle::kCenter);
    Assert::IsTrue(
      TranslateHit(camera, frame, { -0.6F * length, 0.0F, 0.6F * length })
      == GizmoHandle::kNone);
  }

  [TestMethod]
  void AnAxisSeenEndOnIsNotAHandle() {
    const auto camera = FrontCamera();
    const GizmoFrame frame;
    // Y points at the eye: its arrow projects onto the centre.
    const auto length = GizmoLength(camera);

    const auto handle = HitTestGizmo(camera, TransformTool::kTranslate, frame,
      PixelOf(camera, { 0.0F, -0.6F * length, 0.0F }), 1.0F);

    Assert::IsTrue(handle != GizmoHandle::kY);
  }

  [TestMethod]
  void TranslateAlongAnAxisMovesOnlyAlongIt() {
    const auto camera = FrontCamera();
    const GizmoFrame frame;
    const auto length = GizmoLength(camera);
    auto drag = Begin(camera, TransformTool::kTranslate, TransformSpace::kWorld,
      frame, GizmoHandle::kX, PixelOf(camera, { 0.6F * length, 0.0F, 0.0F }),
      { Target(glm::mat4(1.0F), glm::vec3(0.0F)) });

    const auto changed = drag.Update(camera,
      PixelOf(camera, { 0.6F * length + 2.0F, 0.0F, 0.5F }), Snap(false),
      false);

    Assert::IsTrue(changed);
    Assert::IsTrue(
      IsNear(drag.Results()[0].position, { 2.0F, 0.0F, 0.0F }, 1.0e-3F));
    Assert::IsTrue(IsNear(drag.Frame().pivot, { 2.0F, 0.0F, 0.0F }, 1.0e-3F));
    Assert::AreEqual(1, static_cast<int>(drag.Readout().axes));
  }

  [TestMethod]
  void TranslateConvertsThroughTheParent() {
    const auto camera = FrontCamera();
    const auto parent = glm::scale(glm::mat4(1.0F), glm::vec3(2.0F));
    const auto target = Target(parent, glm::vec3(0.5F, 0.0F, 0.0F));
    GizmoFrame frame;
    frame.pivot = glm::vec3(target.world[3]);
    const auto length = GizmoLength(camera);
    auto drag = Begin(camera, TransformTool::kTranslate, TransformSpace::kWorld,
      frame, GizmoHandle::kX,
      PixelOf(camera, frame.pivot + glm::vec3(0.6F * length, 0.0F, 0.0F)),
      { target });

    (void)drag.Update(camera,
      PixelOf(camera, frame.pivot + glm::vec3(0.6F * length + 2.0F, 0.0F, 0.0F)),
      Snap(false), false);

    // Two world metres are one metre in a parent scaled by two.
    Assert::IsTrue(
      IsNear(drag.Results()[0].position, { 1.5F, 0.0F, 0.0F }, 1.0e-3F));
  }

  [TestMethod]
  void WorldSnapPutsThePivotOnTheGridAndLocalSnapRoundsTheOffset() {
    const auto camera = FrontCamera();
    GizmoFrame frame;
    frame.pivot = { 0.1F, 0.0F, 0.0F };
    const auto length = GizmoLength(camera);
    const auto start
      = PixelOf(camera, frame.pivot + glm::vec3(0.6F * length, 0.0F, 0.0F));
    const auto end = PixelOf(
      camera, frame.pivot + glm::vec3(0.6F * length + 0.55F, 0.0F, 0.0F));
    const auto target = Target(glm::mat4(1.0F), frame.pivot);

    auto world = Begin(camera, TransformTool::kTranslate,
      TransformSpace::kWorld, frame, GizmoHandle::kX, start, { target });
    (void)world.Update(camera, end, Snap(true), false);
    auto local = Begin(camera, TransformTool::kTranslate,
      TransformSpace::kLocal, frame, GizmoHandle::kX, start, { target });
    (void)local.Update(camera, end, Snap(true), false);
    auto inverted = Begin(camera, TransformTool::kTranslate,
      TransformSpace::kWorld, frame, GizmoHandle::kX, start, { target });
    (void)inverted.Update(camera, end, Snap(true), true);

    Assert::AreEqual(0.75F, world.Results()[0].position.x, 1.0e-4F);
    Assert::AreEqual(0.6F, local.Results()[0].position.x, 1.0e-4F);
    Assert::AreEqual(0.65F, inverted.Results()[0].position.x, 1.0e-3F);
  }

  [TestMethod]
  void RotationKeepsTheSignedMultiTurnAngle() {
    const auto camera = TopCamera();
    const GizmoFrame frame;
    const auto radius = 0.85F * GizmoLength(camera);
    auto drag = Begin(camera, TransformTool::kRotate, TransformSpace::kWorld,
      frame, GizmoHandle::kZ, PixelOf(camera, { radius, 0.0F, 0.0F }),
      { Target(glm::mat4(1.0F), glm::vec3(0.0F)) });

    TurnAboutZ(camera, drag, 0.0F, 450.0F, Snap(false));

    Assert::AreEqual(450.0F, drag.AppliedDegrees(), 0.05F);
    Assert::AreEqual(450.0F, drag.Readout().values.x, 0.05F);
    const auto expected = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 0, 1));
    Assert::IsTrue(
      std::abs(glm::dot(drag.Results()[0].rotation, expected)) > 0.9999F);

    TurnAboutZ(camera, drag, 450.0F, -450.0F, Snap(false));

    Assert::AreEqual(-450.0F, drag.AppliedDegrees(), 0.05F);
  }

  [TestMethod]
  void RotationSnapsTheAppliedAngle() {
    const auto camera = TopCamera();
    const GizmoFrame frame;
    const auto radius = 0.85F * GizmoLength(camera);
    auto drag = Begin(camera, TransformTool::kRotate, TransformSpace::kWorld,
      frame, GizmoHandle::kZ, PixelOf(camera, { radius, 0.0F, 0.0F }),
      { Target(glm::mat4(1.0F), glm::vec3(0.0F)) });

    TurnAboutZ(camera, drag, 0.0F, 98.0F, Snap(true));

    Assert::AreEqual(105.0F, drag.AppliedDegrees(), 1.0e-3F);
  }

  [TestMethod]
  void RotationThatWouldShearIsRejected() {
    const auto camera = TopCamera();
    const auto parent = glm::scale(glm::mat4(1.0F), glm::vec3(2.0F, 1.0F, 1.0F));
    const auto target = Target(parent, glm::vec3(0.0F));
    const GizmoFrame frame;
    const auto radius = 0.85F * GizmoLength(camera);
    auto drag = Begin(camera, TransformTool::kRotate, TransformSpace::kWorld,
      frame, GizmoHandle::kZ, PixelOf(camera, { radius, 0.0F, 0.0F }),
      { target });

    TurnAboutZ(camera, drag, 0.0F, 45.0F, Snap(false));

    Assert::IsFalse(drag.IsRepresentable());
    Assert::IsTrue(std::abs(glm::dot(drag.Results()[0].rotation,
                     glm::quat(1.0F, 0.0F, 0.0F, 0.0F)))
      > 0.9999F);
  }

  [TestMethod]
  void ScaleMultipliesEachScaleAndSpreadsPositionsFromThePivot() {
    const auto camera = FrontCamera();
    const GizmoFrame frame;
    const auto length = GizmoLength(camera);
    auto drag = Begin(camera, TransformTool::kScale, TransformSpace::kWorld,
      frame, GizmoHandle::kX, PixelOf(camera, { 0.88F * length, 0.0F, 0.0F }),
      { Target(glm::mat4(1.0F), glm::vec3(0.0F)),
        Target(glm::mat4(1.0F), glm::vec3(1.0F, 0.0F, 1.0F)) });

    (void)drag.Update(camera,
      PixelOf(camera, { 1.76F * length, 0.0F, 0.0F }), Snap(false), false);

    Assert::IsTrue(IsNear(drag.Results()[0].scale, { 2.0F, 1.0F, 1.0F }, 1.0e-3F));
    Assert::IsTrue(IsNear(drag.Results()[0].position, glm::vec3(0.0F), 1.0e-4F));
    Assert::IsTrue(
      IsNear(drag.Results()[1].position, { 2.0F, 0.0F, 1.0F }, 1.0e-3F));
  }

  [TestMethod]
  void IdleGizmoIsOccludableAndADragIsDrawnOnTop() {
    const auto camera = TopCamera();
    GizmoVisual idle;
    idle.tool = TransformTool::kRotate;
    ViewOverlay idle_overlay;
    BuildGizmoOverlay(camera, idle, idle_overlay);

    const auto radius = 0.85F * GizmoLength(camera);
    auto drag = Begin(camera, TransformTool::kRotate, TransformSpace::kWorld,
      GizmoFrame {}, GizmoHandle::kZ, PixelOf(camera, { radius, 0.0F, 0.0F }),
      { Target(glm::mat4(1.0F), glm::vec3(0.0F)) });
    TurnAboutZ(camera, drag, 0.0F, 90.0F, Snap(false));
    GizmoVisual dragging = idle;
    dragging.drag = &drag;
    ViewOverlay drag_overlay;
    BuildGizmoOverlay(camera, dragging, drag_overlay);

    Assert::IsFalse(idle_overlay.scene.IsEmpty());
    Assert::IsTrue(idle_overlay.top.IsEmpty());
    Assert::IsTrue(drag_overlay.scene.IsEmpty());
    Assert::IsFalse(drag_overlay.top.triangles.empty());
  }
};

} // namespace InteropTests

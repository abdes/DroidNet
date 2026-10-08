//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <memory>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vector_relational.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Scene.h>

#include <EditorModule/EditorView.h>
#include <EditorModule/ViewFraming.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::interop::module::CameraViewPreset;
using oxygen::interop::module::EditorFramingOutcome;
using oxygen::interop::module::EditorView;
using oxygen::interop::module::kEmptySceneFrameRadius;
using oxygen::interop::module::kFramePointRadius;
using oxygen::interop::module::ResolveNodesFrameSphere;
using oxygen::interop::module::ResolveSceneFrameSphere;

namespace {

auto MakeScene(const char* name) -> std::shared_ptr<oxygen::scene::Scene> {
  return std::make_shared<oxygen::scene::Scene>(name, 16U);
}

auto IsNear(const glm::vec3& a, const glm::vec3& b, const float tolerance)
  -> bool {
  return glm::all(glm::epsilonEqual(a, b, tolerance));
}

//! Runs a framing move to its end.
void Settle(EditorView& view) {
  for (int frame = 0; frame < 30; ++frame) {
    view.AdvanceFraming(1.0F / 60.0F);
  }
}

} // namespace

namespace InteropTests {

//! Frame Selected and Frame All bounds, and the eased editor camera move that
//! frames them without touching authored data.
[TestClass]
public ref class ViewFramingTests {
public:
  [TestMethod]
  void NodeWithoutGeometryFramesItsPositionWithTheDefaultExtent() {
    const auto scene = MakeScene("Authored");
    auto light = scene->CreateNode("Light");
    (void)light.GetTransform().SetLocalPosition({ 3.0F, 4.0F, 1.0F });
    const std::vector handles { light.GetHandle() };

    const auto sphere = ResolveNodesFrameSphere(*scene, handles);

    Assert::IsTrue(sphere.has_value());
    Assert::IsTrue(IsNear(sphere->center, { 3.0F, 4.0F, 1.0F }, 1.0e-5F));
    Assert::AreEqual(kFramePointRadius * std::sqrt(3.0F), sphere->radius,
      1.0e-5F);
  }

  [TestMethod]
  void TransformedHierarchyFramesWorldPositions() {
    const auto scene = MakeScene("Authored");
    auto parent = scene->CreateNode("Group");
    (void)parent.GetTransform().SetLocalPosition({ 10.0F, 0.0F, 0.0F });
    auto child = scene->CreateChildNode(parent, "Camera");
    Assert::IsTrue(child.has_value());
    (void)child->GetTransform().SetLocalPosition({ 0.0F, 2.0F, 0.0F });
    const std::vector handles { child->GetHandle() };

    const auto sphere = ResolveNodesFrameSphere(*scene, handles);

    Assert::IsTrue(sphere.has_value());
    Assert::IsTrue(IsNear(sphere->center, { 10.0F, 2.0F, 0.0F }, 1.0e-4F));
  }

  [TestMethod]
  void MissingNodesHaveNothingToFrame() {
    const auto scene = MakeScene("Authored");
    auto node = scene->CreateNode("Gone");
    const std::vector handles { node.GetHandle() };
    (void)scene->DestroyNode(node);

    Assert::IsFalse(ResolveNodesFrameSphere(*scene, handles).has_value());
  }

  [TestMethod]
  void EmptySceneFramesTheOrigin() {
    const auto scene = MakeScene("Authored");

    const auto sphere = ResolveSceneFrameSphere(*scene);

    Assert::IsTrue(sphere.has_value());
    Assert::IsTrue(IsNear(sphere->center, glm::vec3 { 0.0F }, 1.0e-6F));
    Assert::AreEqual(kEmptySceneFrameRadius, sphere->radius);
  }

  [TestMethod]
  void SceneWithoutGeometryFramesEveryNode() {
    const auto scene = MakeScene("Authored");
    auto a = scene->CreateNode("A");
    (void)a.GetTransform().SetLocalPosition({ -4.0F, 0.0F, 0.0F });
    auto b = scene->CreateNode("B");
    (void)b.GetTransform().SetLocalPosition({ 4.0F, 0.0F, 0.0F });

    const auto sphere = ResolveSceneFrameSphere(*scene);

    Assert::IsTrue(sphere.has_value());
    Assert::IsTrue(IsNear(sphere->center, glm::vec3 { 0.0F }, 1.0e-5F));
    Assert::IsTrue(sphere->radius > 4.0F);
  }

  [TestMethod]
  void PerspectiveFramingKeepsTheDirectionAndFitsTheSphere() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    EditorView view(EditorView::Config {
      .name = "Pane", .width = 400, .height = 200 });
    view.Initialize(*authored, *cameras);
    auto transform = view.GetCameraNode().GetTransform();
    const auto rotation
      = transform.GetLocalRotation().value_or(glm::quat { 1, 0, 0, 0 });
    const glm::vec3 center { 5.0F, -2.0F, 1.0F };

    const auto outcome = view.BeginFraming(center, 2.0F);
    Settle(view);

    Assert::IsTrue(outcome == EditorFramingOutcome::kFramed);
    Assert::IsTrue(IsNear(view.GetFocusPoint(), center, 1.0e-4F));
    const auto position
      = transform.GetLocalPosition().value_or(glm::vec3 { 0.0F });
    const glm::vec3 forward = rotation * oxygen::space::look::Forward;
    const glm::vec3 to_center = center - position;
    // Same orientation, looking straight at the centre.
    Assert::IsTrue(IsNear(glm::normalize(to_center), forward, 1.0e-4F));
    // 60 degrees vertical is the narrower field of view at 2:1: the sphere
    // with a 10% margin fits it exactly.
    const float expected = 2.0F * 1.1F / std::sin(glm::radians(30.0F));
    Assert::AreEqual(expected, glm::length(to_center), 1.0e-3F);
  }

  [TestMethod]
  void OrthographicFramingResizesToFit() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    EditorView view(EditorView::Config { .name = "Pane",
      .width = 200,
      .height = 400,
      .camera_preset = CameraViewPreset::kTop });
    view.Initialize(*authored, *cameras);

    const auto outcome = view.BeginFraming({ 1.0F, 1.0F, 0.0F }, 3.0F);
    Settle(view);

    Assert::IsTrue(outcome == EditorFramingOutcome::kFramed);
    // A 1:2 pane fits the width: the half-height doubles.
    Assert::AreEqual(3.0F * 1.1F * 2.0F, view.GetOrthoHalfHeight(), 1.0e-4F);
    Assert::IsTrue(
      IsNear(view.GetFocusPoint(), { 1.0F, 1.0F, 0.0F }, 1.0e-4F));
  }

  [TestMethod]
  void NavigationDuringTheMoveTakesOver() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    EditorView view(EditorView::Config {
      .name = "Pane", .width = 320, .height = 200 });
    view.Initialize(*authored, *cameras);
    (void)view.BeginFraming({ 50.0F, 0.0F, 0.0F }, 1.0F);
    view.AdvanceFraming(1.0F / 60.0F);

    // The user moves the camera: the framing move must not fight it.
    auto transform = view.GetCameraNode().GetTransform();
    const glm::vec3 navigated { -20.0F, -20.0F, 20.0F };
    (void)transform.SetLocalPosition(navigated);
    Settle(view);

    Assert::IsTrue(IsNear(
      transform.GetLocalPosition().value_or(glm::vec3 { 0.0F }), navigated,
      1.0e-6F));
  }
};

} // namespace InteropTests

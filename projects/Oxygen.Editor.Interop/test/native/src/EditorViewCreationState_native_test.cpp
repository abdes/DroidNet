//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <memory>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vector_relational.hpp>

#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Scene.h>

#include <EditorModule/EditorView.h>
#include <EditorModule/ViewportInset.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::interop::module::CameraViewPreset;
using oxygen::interop::module::EditorCameraState;
using oxygen::interop::module::EditorView;
using oxygen::interop::module::ResolveInsetViewport;

namespace {

auto MakeScene(const char* name) -> std::shared_ptr<oxygen::scene::Scene> {
  return std::make_shared<oxygen::scene::Scene>(name, 16U);
}

auto IsNear(const glm::vec3& a, const glm::vec3& b) -> bool {
  return glm::all(glm::epsilonEqual(a, b, 1.0e-5F));
}

} // namespace

namespace InteropTests {

//! A recreated or restored pane starts as it was left: the view created with
//! a kept editor camera state and preset presents them from its first frame.
[TestClass]
public ref class EditorViewCreationStateTests {
public:
  [TestMethod]
  void ViewStartsFromTheKeptEditorCameraState() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    const EditorCameraState kept {
      .position = { 4.0F, -6.0F, 9.0F },
      .rotation = glm::angleAxis(0.7F, glm::vec3 { 0.0F, 0.0F, 1.0F }),
      .focus_point = { 1.0F, 2.0F, 0.0F },
      .ortho_half_height = 7.5F,
    };
    EditorView view(EditorView::Config {
      .name = "Pane",
      .width = 320,
      .height = 200,
      .camera_preset = CameraViewPreset::kTop,
      .editor_camera = kept,
    });

    view.Initialize(*authored, *cameras);

    Assert::IsTrue(view.GetCameraViewPreset() == CameraViewPreset::kTop);
    Assert::IsTrue(view.GetCameraNode()
        .GetCameraAs<oxygen::scene::OrthographicCamera>()
        .has_value(),
      "The preset sets the camera projection.");
    const auto state = view.GetEditorCameraState();
    Assert::IsTrue(state.has_value());
    Assert::IsTrue(IsNear(state->position, kept.position));
    Assert::IsTrue(IsNear(state->focus_point, kept.focus_point));
    Assert::IsTrue(std::abs(glm::dot(state->rotation, kept.rotation)) > 0.9999F);
    Assert::AreEqual(kept.ortho_half_height, state->ortho_half_height);
  }

  [TestMethod]
  void NonFiniteKeptStateLeavesTheDefaultCamera() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    EditorView view(EditorView::Config {
      .name = "Pane",
      .width = 320,
      .height = 200,
      .editor_camera = EditorCameraState { .position = { nan, 0.0F, 0.0F } },
    });

    view.Initialize(*authored, *cameras);

    const auto state = view.GetEditorCameraState();
    Assert::IsTrue(state.has_value());
    Assert::IsTrue(std::isfinite(state->position.x));
  }
};

//! The camera preview inset takes the bottom-right corner of its host surface.
[TestClass]
public ref class ViewportInsetTests {
public:
  [TestMethod]
  void InsetSitsInTheBottomRightCorner() {
    const auto inset = ResolveInsetViewport(1000.0F, 600.0F);

    Assert::IsTrue(inset.has_value());
    Assert::AreEqual(300.0F, inset->width);
    Assert::AreEqual(180.0F, inset->height);
    Assert::AreEqual(1000.0F - 12.0F, inset->top_left_x + inset->width);
    Assert::AreEqual(600.0F - 12.0F, inset->top_left_y + inset->height);
  }

  [TestMethod]
  void SmallSurfaceHasNoInset() {
    Assert::IsFalse(ResolveInsetViewport(300.0F, 200.0F).has_value());
  }
};

} // namespace InteropTests

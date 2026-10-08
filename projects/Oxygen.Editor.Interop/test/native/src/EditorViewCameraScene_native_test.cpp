//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Scene/Scene.h>

#include <EditorModule/EditorView.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::interop::module::EditorView;

namespace {

auto MakeScene(const char* name) -> std::shared_ptr<oxygen::scene::Scene> {
  return std::make_shared<oxygen::scene::Scene>(name, 16U);
}

auto MakeView() -> std::unique_ptr<EditorView> {
  return std::make_unique<EditorView>(
    EditorView::Config { .name = "Pane", .width = 320, .height = 200 });
}

} // namespace

namespace InteropTests {

//! The editor navigation camera lives in the editor-owned camera scene, never
//! in the authored scene, and survives authored scene replacement.
[TestClass]
public ref class EditorViewCameraSceneTests {
public:
  [TestMethod]
  void EditorCameraIsCreatedInTheCameraScene() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    auto view = MakeView();

    view->Initialize(*authored, *cameras);

    auto camera = view->GetCameraNode();
    Assert::IsTrue(camera.IsAlive());
    Assert::IsTrue(camera.HasCamera());
    Assert::IsTrue(cameras->Contains(camera));
    Assert::IsFalse(authored->Contains(camera));
    Assert::IsTrue(authored->IsEmpty(), "The authored scene holds only authored nodes.");
    Assert::AreEqual(std::size_t { 1 }, cameras->GetNodeCount());
  }

  [TestMethod]
  void SceneReplacementKeepsTheEditorCameraPose() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    auto view = MakeView();
    view->Initialize(*authored, *cameras);
    auto camera = view->GetCameraNode();
    const glm::vec3 position { 3.0F, -7.0F, 2.5F };
    (void)camera.GetTransform().SetLocalPosition(position);

    const auto replacement = MakeScene("Replacement");
    view->RetargetScene(*replacement);

    auto retained = view->GetCameraNode();
    Assert::IsTrue(retained.IsAlive());
    Assert::IsTrue(retained.GetHandle() == camera.GetHandle());
    const auto kept = retained.GetTransform().GetLocalPosition();
    Assert::IsTrue(kept.has_value());
    Assert::IsTrue(*kept == position);
    Assert::IsTrue(replacement->IsEmpty());
  }

  [TestMethod]
  void ReleasingTheViewRemovesItsEditorCamera() {
    const auto authored = MakeScene("Authored");
    const auto cameras = MakeScene("Cameras");
    auto view = MakeView();
    view->Initialize(*authored, *cameras);

    view->ReleaseResources();

    Assert::IsTrue(cameras->IsEmpty());
  }
};

} // namespace InteropTests

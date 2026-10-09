//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/gtc/matrix_transform.hpp>

#include <EditorModule/PaneRenderPolicy.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::ViewId;
using oxygen::interop::module::PaneFingerprint;
using oxygen::interop::module::PaneRenderPolicy;

namespace {

constexpr ViewId kPane { 7U };
constexpr ViewId kOtherPane { 8U };

auto MakeFingerprint(const float camera_x) -> PaneFingerprint {
  return PaneFingerprint {
    .view = glm::translate(glm::mat4 { 1.0F }, { camera_x, 0.0F, 0.0F }),
    .projection = glm::mat4 { 1.0F },
    .width = 640.0F,
    .height = 480.0F,
  };
}

//! A policy whose two panes have rendered `MakeFingerprint(0)`.
auto MakeSettledPolicy() -> PaneRenderPolicy {
  PaneRenderPolicy policy;
  policy.BeginFrame(false);
  policy.MarkRendered(kPane, MakeFingerprint(0.0F));
  policy.MarkRendered(kOtherPane, MakeFingerprint(0.0F));
  return policy;
}

} // namespace

namespace InteropTests {

//! Viewport panes render when what they show may have changed and otherwise
//! keep their last image.
[TestClass]
public ref class PaneRenderPolicyTests {
public:
  [TestMethod]
  void NewPaneRenders() {
    PaneRenderPolicy policy;
    policy.BeginFrame(false);

    Assert::IsTrue(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
  }

  [TestMethod]
  void UnchangedPaneStaysIdle() {
    auto policy = MakeSettledPolicy();
    policy.BeginFrame(false);

    Assert::IsFalse(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
    Assert::IsTrue(policy.IsCurrent(kPane, MakeFingerprint(0.0F)));
  }

  [TestMethod]
  void CameraChangeRendersOnlyThatPane() {
    auto policy = MakeSettledPolicy();
    policy.BeginFrame(false);

    Assert::IsTrue(policy.NeedsRender(kPane, MakeFingerprint(1.0F)));
    Assert::IsFalse(policy.IsCurrent(kPane, MakeFingerprint(1.0F)));
    Assert::IsFalse(policy.NeedsRender(kOtherPane, MakeFingerprint(0.0F)));
  }

  [TestMethod]
  void SceneChangeRendersEveryPaneForOneFrame() {
    auto policy = MakeSettledPolicy();

    policy.BeginFrame(true);
    Assert::IsTrue(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
    Assert::IsTrue(policy.NeedsRender(kOtherPane, MakeFingerprint(0.0F)));

    policy.MarkRendered(kPane, MakeFingerprint(0.0F));
    policy.MarkRendered(kOtherPane, MakeFingerprint(0.0F));
    policy.BeginFrame(false);
    Assert::IsFalse(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
  }

  [TestMethod]
  void InvalidatedPaneRendersUntilItRenders() {
    auto policy = MakeSettledPolicy();
    policy.Invalidate(kPane);
    policy.BeginFrame(false);

    Assert::IsTrue(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
    Assert::IsFalse(policy.NeedsRender(kOtherPane, MakeFingerprint(0.0F)));

    policy.MarkRendered(kPane, MakeFingerprint(0.0F));
    Assert::IsFalse(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
  }

  [TestMethod]
  void AlwaysRenderRendersSettledPanes() {
    auto policy = MakeSettledPolicy();
    policy.SetAlwaysRender(true);
    policy.BeginFrame(false);

    Assert::IsTrue(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
  }

  [TestMethod]
  void ForgottenPaneRendersAgain() {
    auto policy = MakeSettledPolicy();
    policy.Forget(kPane);
    policy.BeginFrame(false);

    Assert::IsTrue(policy.NeedsRender(kPane, MakeFingerprint(0.0F)));
    Assert::IsFalse(policy.IsCurrent(kPane, MakeFingerprint(0.0F)));
  }
};

} // namespace InteropTests

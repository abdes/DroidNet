// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Viewports.Unit.Tests;

[TestClass]
[TestCategory("Viewport Tools")]
internal sealed class SceneViewsTests
{
    [TestMethod]
    public async Task Frame_ForADocumentWithoutAnEditor_ReturnsFalse()
    {
        var views = new SceneViews();

        var framed = await views.FrameAsync(DocumentId.New(), [NodeReference.To(NodeId.New())]).ConfigureAwait(false);

        _ = framed.Should().BeFalse();
    }

    [TestMethod]
    public async Task RunCameraCommand_ForADocumentWithoutAnEditor_ReturnsFalse()
    {
        var views = new SceneViews();

        var ran = await views.RunCameraCommandAsync(DocumentId.New(), NodeId.New(), SceneCameraCommand.LookThrough).ConfigureAwait(false);

        _ = ran.Should().BeFalse();
    }

    [TestMethod]
    public void CameraStateOf_ForADocumentWithoutAnEditor_ReturnsNull()
        => _ = new SceneViews().CameraStateOf(DocumentId.New()).Should().BeNull();
}

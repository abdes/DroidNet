// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Moq;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests;

/// <summary>
/// The engine shows the active document (model editor 6, ME-2): activating a scene's tab or a model's tab shows its
/// scene, until the window stops being followed.
/// </summary>
[TestClass]
public sealed class ShownDocumentFollowerTests
{
    [TestMethod]
    public async Task ActivatingADocument_ShowsItsScene()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var harbor = await documents.OpenAsync(new Scene { Name = "Harbor" }.Dehydrate()).ConfigureAwait(false);
        var car = await documents.OpenModelAsync(AssetPath.Parse("/Content/Car.glb")).ConfigureAwait(false);
        var area = new TestDocumentArea(harbor, car);
        var preview = new Mock<IScenePreview>();
        using var follower = new ShownDocumentFollower(area, scenes, preview.Object);
        preview.Verify(value => value.Show(harbor.Id), Times.Once(), "the document active when following starts shows");

        area.Activate(car);

        preview.Verify(value => value.Show(car.Id), Times.Once());
    }

    [TestMethod]
    public async Task Disposed_ShowsNothing()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var car = await documents.OpenModelAsync(AssetPath.Parse("/Content/Car.glb")).ConfigureAwait(false);
        var area = new TestDocumentArea();
        var preview = new Mock<IScenePreview>();
        var follower = new ShownDocumentFollower(area, scenes, preview.Object);

        follower.Dispose();
        area.Activate(car);

        preview.Verify(value => value.Show(It.IsAny<DocumentId>()), Times.Never());
    }
}

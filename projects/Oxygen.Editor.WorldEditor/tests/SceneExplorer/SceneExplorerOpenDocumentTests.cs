// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Ensures tab activation reuses open authoring instead of rereading and discarding it.</summary>
[TestClass]
public sealed class SceneExplorerOpenDocumentTests
{
    [TestMethod]
    public async Task SwitchingBackToAnOpenScenePreservesUnsavedModelAndAvoidsDiskReload()
    {
        var project = new Project(new ProjectInfo("Open scenes", Category.Games, "H:/OpenSceneTests", "preview.png")) { Name = "Open scenes" };
        var first = new Scene(project) { Name = "First" };
        var second = new Scene(project) { Name = "Second" };
        var node = new SceneNode(first) { Name = "Unsaved node" };
        node.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(9, 8, 7);
        first.RootNodes.Add(node);
        project.Scenes.Add(first);
        project.Scenes.Add(second);
        var firstMetadata = new SceneDocumentMetadata(first.Id) { IsDirty = true };
        var secondMetadata = new SceneDocumentMetadata(second.Id);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([firstMetadata, secondMetadata]);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(firstMetadata)).Returns(first);
        _ = sync.Setup(value => value.GetDocumentScene(secondMetadata)).Returns(second);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), It.IsAny<SceneDocumentMetadata>())).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: true);
        using var explorer = new SceneExplorerViewModel(
            manager.Object,
            new StrongReferenceMessenger(),
            Mock.Of<IRouter>(),
            documents.Object,
            default,
            sync.Object,
            Mock.Of<ISceneExplorerService>(),
            new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(second).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);

        _ = explorer.Scene.Should().NotBeNull();
        _ = explorer.Scene!.AttachedObject.Should().BeSameAs(first);
        _ = firstMetadata.IsDirty.Should().BeTrue();
        _ = first.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.Should().Be(new Vector3(9, 8, 7));
        manager.Verify(value => value.LoadSceneAsync(It.IsAny<Scene>()), Times.Never);
        sync.Verify(value => value.SyncSceneWhenReadyAsync(first, It.IsAny<CancellationToken>()), Times.Exactly(2));
        sync.Verify(value => value.SyncSceneWhenReadyAsync(second, It.IsAny<CancellationToken>()), Times.Once);
        _ = (await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false)).Should().NotBeNull();
    }
}

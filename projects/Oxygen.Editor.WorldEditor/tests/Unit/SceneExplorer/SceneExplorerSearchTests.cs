// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures scene search reveals collapsed matches and restores expansion on clear.</summary>
[TestClass]
public sealed class SceneExplorerSearchTests
{
    [TestMethod]
    public async Task SearchAsync_RevealsCollapsedDescendantAndRestoresExpansion()
    {
        var project = new Project(new ProjectInfo("Search", Category.Games, "H:/SearchTests", "preview.png")) { Name = "Search" };
        var scene = new Scene(project) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent", IsExpanded = false };
        var child = new SceneNode(scene) { Name = "FindMe" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        project.Scenes.Add(scene);
        var metadata = new SceneDocumentMetadata(scene.Id);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
        _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(metadata)).Returns(scene);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata)).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents.Object, default, sync.Object, new SceneSelectionService(), Mock.Of<ISceneDocumentCommandService>());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var count = await explorer.SearchAsync("FindMe").ConfigureAwait(false);

        _ = count.Should().Be(1);
        var parentAdapter = await explorer.FindAdapterByNodeIdAsync(parent.Id).ConfigureAwait(false);
        _ = parentAdapter!.IsExpanded.Should().BeTrue("search should reveal the collapsed matching descendant");

        await explorer.ClearSearchAsync().ConfigureAwait(false);

        _ = parentAdapter.IsExpanded.Should().BeFalse("clearing search should restore the prior expansion state");
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures tab activation reuses open authoring instead of rereading and discarding it.</summary>
[TestClass]
public sealed class SceneExplorerOpenDocumentTests
{
    [TestMethod]
    public async Task CommitRenameAsync_RoutesNodeRenameThroughDocumentCommand()
    {
        var project = new Project(new ProjectInfo("Rename", Category.Games, "H:/RenameTreeTests", "preview.png")) { Name = "Rename" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Original" };
        scene.RootNodes.Add(node);
        var metadata = SceneTabs.Open(scene);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
        _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(metadata.DocumentId)).Returns(scene);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata.DocumentId)).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
        var commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        _ = commands
            .Setup(value => value.RenameNodeAsync(It.IsAny<SceneDocumentCommandContext>(), node.Id, "Renamed"))
            .ReturnsAsync(SceneCommandResult.Success);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents.Object, default, sync.Object, new SceneSelectionService(), commands.Object);

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "  Renamed  ").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        commands.Verify(value => value.RenameNodeAsync(It.IsAny<SceneDocumentCommandContext>(), node.Id, "Renamed"), Times.Once);
    }

    [TestMethod]
    public async Task CommitRenameAsync_RejectsEmptyNameWithoutCommand()
    {
        var project = new Project(new ProjectInfo("Rename", Category.Games, "H:/RenameTreeTests", "preview.png")) { Name = "Rename" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Original" };
        scene.RootNodes.Add(node);
        var metadata = SceneTabs.Open(scene);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
        _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(metadata.DocumentId)).Returns(scene);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata.DocumentId)).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
        var commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents.Object, default, sync.Object, new SceneSelectionService(), commands.Object);

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "   ").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        commands.Verify(value => value.RenameNodeAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<Guid>(), It.IsAny<string>()), Times.Never);
    }

    [TestMethod]
    public async Task CommitRenameAsync_UnchangedNameSkipsCommand()
    {
        var project = new Project(new ProjectInfo("Rename", Category.Games, "H:/RenameTreeTests", "preview.png")) { Name = "Rename" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Original" };
        scene.RootNodes.Add(node);
        var metadata = SceneTabs.Open(scene);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
        _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(metadata.DocumentId)).Returns(scene);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata.DocumentId)).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
        var commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents.Object, default, sync.Object, new SceneSelectionService(), commands.Object);

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "Original").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        commands.Verify(value => value.RenameNodeAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<Guid>(), It.IsAny<string>()), Times.Never);
    }

    [TestMethod]
    public async Task ReloadMessageReplacesTreeAdaptersWithoutReadingDiskOrRequiringRuntime()
    {
        var project = new Project(new ProjectInfo("Reload", Category.Games, "H:/ReloadTreeTests", "preview.png")) { Name = "Reload" };
        var original = new Scene(project) { Name = "Scene" };
        var originalNode = new SceneNode(original) { Name = "Original node" };
        original.RootNodes.Add(originalNode);
        var replacement = Scene.CreateAndHydrate(project, original.Dehydrate());
        replacement.RootNodes[0].Name = "Reloaded node";
        var metadata = SceneTabs.Open(original, reloads: [replacement]);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
        _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
        var current = original;
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(metadata.DocumentId)).Returns(() => current);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata.DocumentId)).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
        var messenger = new StrongReferenceMessenger();
        using var explorer = new SceneExplorerViewModel(manager.Object, messenger, Mock.Of<IRouter>(), documents.Object, default, sync.Object, new SceneSelectionService(), Mock.Of<ISceneDocumentCommandService>());
        await explorer.HandleDocumentOpenedAsync(original).ConfigureAwait(false);
        var previousAdapter = await explorer.FindAdapterByNodeIdAsync(originalNode.Id).ConfigureAwait(false);
        _ = (await metadata.Document!.ReloadAsync().ConfigureAwait(false)).Succeeded.Should().BeTrue();
        current = replacement;

        var request = messenger.Send(new SceneReloadedMessage(replacement, metadata, default));

        _ = request.HasReceivedResponse.Should().BeTrue();
        _ = (await request.Response.ConfigureAwait(false)).Should().BeTrue();
        _ = explorer.Scene!.AttachedObject.Should().BeSameAs(replacement);
        var adapter = await explorer.FindAdapterByNodeIdAsync(originalNode.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeSameAs(previousAdapter);
        _ = ((SceneNodeAdapter)adapter!).AttachedObject.Should().BeSameAs(replacement.RootNodes[0]);
        _ = adapter.Label.Should().Be("Reloaded node");
    }

    [TestMethod]
    public async Task SwitchingBackToAnOpenScenePreservesUnsavedModelAndAvoidsDiskReload()
    {
        var project = new Project(new ProjectInfo("Open scenes", Category.Games, "H:/OpenSceneTests", "preview.png")) { Name = "Open scenes" };
        var first = new Scene(project) { Name = "First" };
        var second = new Scene(project) { Name = "Second" };
        var node = new SceneNode(first) { Name = "Unsaved node" };
        node.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(9, 8, 7);
        first.RootNodes.Add(node);
        var firstMetadata = SceneTabs.Open(first);
        firstMetadata.Document!.History.AddChange("Unsaved edit", static () => { });
        var secondMetadata = SceneTabs.Open(second);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([firstMetadata, secondMetadata]);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(firstMetadata.DocumentId)).Returns(first);
        _ = sync.Setup(value => value.GetDocumentScene(secondMetadata.DocumentId)).Returns(second);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), It.IsAny<Guid>())).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: true);
        using var explorer = new SceneExplorerViewModel(
            manager.Object,
            new StrongReferenceMessenger(),
            Mock.Of<IRouter>(),
            documents.Object,
            default,
            sync.Object,
            new SceneSelectionService(),
            Mock.Of<ISceneDocumentCommandService>());

        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(second).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);

        _ = explorer.Scene.Should().NotBeNull();
        _ = explorer.Scene!.AttachedObject.Should().BeSameAs(first);
        _ = firstMetadata.Document.IsDirty.Should().BeTrue();
        _ = first.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.Should().Be(new Vector3(9, 8, 7));
        sync.Verify(value => value.SyncSceneWhenReadyAsync(first, It.IsAny<CancellationToken>()), Times.Exactly(2));
        sync.Verify(value => value.SyncSceneWhenReadyAsync(second, It.IsAny<CancellationToken>()), Times.Once);
        _ = (await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false)).Should().NotBeNull();
    }
}

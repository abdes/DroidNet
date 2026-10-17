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
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures tab activation reuses open authoring instead of rereading and discarding it.</summary>
[TestClass]
public sealed class SceneExplorerOpenDocumentTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task CommitRenameAsync_RenamesTheNodeInOneUndoStep()
    {
        var project = new Project(new ProjectInfo("Rename", Category.Games, "H:/RenameTreeTests", "preview.png")) { Name = "Rename" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Original" };
        scene.RootNodes.Add(node);
        using var metadata = SceneTabs.Open(scene);
        scene = metadata.Scene;
        node = metadata.NodeOf(node);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents, metadata.Documents, default, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "  Renamed  ").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Name.Should().Be("Renamed");
        _ = metadata.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task CommitRenameAsync_EmptyNameFailsAndChangesNothing()
    {
        var project = new Project(new ProjectInfo("Rename", Category.Games, "H:/RenameTreeTests", "preview.png")) { Name = "Rename" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Original" };
        scene.RootNodes.Add(node);
        using var metadata = SceneTabs.Open(scene);
        scene = metadata.Scene;
        node = metadata.NodeOf(node);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents, metadata.Documents, default, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "   ").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = node.Name.Should().Be("Original");
        _ = metadata.Document.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CommitRenameAsync_UnchangedNameRecordsNothing()
    {
        var project = new Project(new ProjectInfo("Rename", Category.Games, "H:/RenameTreeTests", "preview.png")) { Name = "Rename" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Original" };
        scene.RootNodes.Add(node);
        using var metadata = SceneTabs.Open(scene);
        scene = metadata.Scene;
        node = metadata.NodeOf(node);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents, metadata.Documents, default, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "Original").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Name.Should().Be("Original");
        _ = metadata.Document.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ReloadReplacesTreeAdaptersWithoutRequiringRuntime()
    {
        var project = new Project(new ProjectInfo("Reload", Category.Games, "H:/ReloadTreeTests", "preview.png")) { Name = "Reload" };
        var original = new Scene(project) { Name = "Scene" };
        var originalNode = new SceneNode(original) { Name = "Original node" };
        original.RootNodes.Add(originalNode);
        using var metadata = SceneTabs.Open(original);
        var replacement = Scene.CreateAndHydrate(project, original.Dehydrate());
        replacement.RootNodes[0].Name = "Reloaded node";
        metadata.WriteFile(replacement);
        original = metadata.Scene;
        originalNode = metadata.NodeOf(originalNode);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents, metadata.Documents, default, new SceneSelectionService());
        await explorer.HandleDocumentOpenedAsync(original).ConfigureAwait(false);
        var previousAdapter = await explorer.FindAdapterByNodeIdAsync(originalNode.Id).ConfigureAwait(false);

        _ = (await metadata.Document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var reloaded = metadata.Scene;

        // The explorer follows the reload on its own; it shows the new scene once its tree is rebuilt.
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        while (!ReferenceEquals(explorer.Scene?.AttachedObject, reloaded))
        {
            await Task.Delay(10, timeout.Token).ConfigureAwait(false);
        }

        _ = explorer.Scene!.AttachedObject.Should().BeSameAs(reloaded);
        var adapter = await explorer.FindAdapterByNodeIdAsync(originalNode.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeSameAs(previousAdapter);
        _ = ((SceneNodeAdapter)adapter!).AttachedObject.Should().BeSameAs(reloaded.RootNodes[0]);
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
        using var firstMetadata = SceneTabs.Open(first);
        first = firstMetadata.Scene;
        node = firstMetadata.NodeOf(node);
        firstMetadata.Document.History.AddChange("Unsaved edit", static () => { });
        using var secondMetadata = SceneTabs.Open(second);
        second = secondMetadata.Scene;
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new TestDocumentArea(firstMetadata.Document, secondMetadata.Document);
        using var explorer = new SceneExplorerViewModel(
            manager.Object,
            new StrongReferenceMessenger(),
            Mock.Of<IRouter>(),
            documents,
            firstMetadata.Documents,
            default,
            new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(second).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);

        _ = explorer.Scene.Should().NotBeNull();
        _ = explorer.Scene!.AttachedObject.Should().BeSameAs(first);
        _ = firstMetadata.Document.IsDirty.Should().BeTrue();
        _ = first.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.Should().Be(new Vector3(9, 8, 7));
        _ = (await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false)).Should().NotBeNull();
    }
}

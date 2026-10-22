// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>Ensures tab activation reuses open authoring instead of rereading and discarding it.</summary>
[TestClass]
public sealed class SceneExplorerOpenDocumentTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task CommitRenameAsync_RenamesTheNodeInOneUndoStep()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Original");
        scene.RootNodes.Add(node);
        using var metadata = TestScenes.Open(scene);
        scene = metadata.Scene;
        node = metadata.NodeOf(node);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), documents, metadata.Documents, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "  Renamed  ").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Metadata.Name.Should().Be("Renamed");
        _ = metadata.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task CommitRenameAsync_EmptyNameFailsAndChangesNothing()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Original");
        scene.RootNodes.Add(node);
        using var metadata = TestScenes.Open(scene);
        scene = metadata.Scene;
        node = metadata.NodeOf(node);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), documents, metadata.Documents, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "   ").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = node.Metadata.Name.Should().Be("Original");
        _ = metadata.Document.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CommitRenameAsync_UnchangedNameRecordsNothing()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Original");
        scene.RootNodes.Add(node);
        using var metadata = TestScenes.Open(scene);
        scene = metadata.Scene;
        node = metadata.NodeOf(node);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), documents, metadata.Documents, new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var result = await explorer.CommitRenameAsync(adapter!, "Original").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Metadata.Name.Should().Be("Original");
        _ = metadata.Document.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ReloadReplacesTreeAdaptersWithoutRequiringRuntime()
    {
        var original = new Scene { Name = "Scene" };
        var originalNode = new SceneNode(original, "Original node");
        original.RootNodes.Add(originalNode);
        using var metadata = TestScenes.Open(original);
        var replacement = Scene.CreateAndHydrate(original.Dehydrate());
        replacement.RootNodes[0].Metadata.Name = "Reloaded node";
        metadata.WriteFile(replacement);
        original = metadata.Scene;
        originalNode = metadata.NodeOf(originalNode);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), documents, metadata.Documents, new SceneSelectionService());
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
        var first = new Scene { Name = "First" };
        var second = new Scene { Name = "Second" };
        var node = new SceneNode(first, "Unsaved node");
        node.Transform.LocalPosition = new Vector3(9, 8, 7);
        first.RootNodes.Add(node);
        using var firstMetadata = TestScenes.Open(first);
        first = firstMetadata.Scene;
        node = firstMetadata.NodeOf(node);
        firstMetadata.Document.History.AddChange("Unsaved edit", static () => { });
        using var secondMetadata = TestScenes.Open(second);
        second = secondMetadata.Scene;
        var documents = new TestDocumentArea(firstMetadata.Document, secondMetadata.Document);
        using var explorer = new SceneExplorerViewModel(
            Mock.Of<IRouter>(),
            documents,
            firstMetadata.Documents,
            new SceneSelectionService());

        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(second).ConfigureAwait(false);
        await explorer.HandleDocumentOpenedAsync(first).ConfigureAwait(false);

        _ = explorer.Scene.Should().NotBeNull();
        _ = explorer.Scene!.AttachedObject.Should().BeSameAs(first);
        _ = firstMetadata.Document.IsDirty.Should().BeTrue();
        _ = first.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = node.Transform.LocalPosition.Should().Be(new Vector3(9, 8, 7));
        _ = (await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false)).Should().NotBeNull();
    }
}

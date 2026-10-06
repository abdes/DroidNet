// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.Controls.Menus;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.SceneExplorer;

/// <summary>Verifies captured context-menu targets without physical input or focus dependencies.</summary>
[TestClass]
public sealed class SceneExplorerContextMenuTests : DroidNet.Tests.VisualUserInterfaceTests
{
    [TestMethod]
    public Task ContextMenu_SelectionChanges_RejectsCapturedDelete() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(second);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        var menu = explorer.BuildContextMenuSource(row!);
        var command = GetMenuCommand(menu, "Delete node");
        var dismissed = false;
        explorer.ContextMenuInvalidated += (_, _) => dismissed = true;

        _ = await explorer.SetSelectedNodes([second.Id]).ConfigureAwait(true);
        await command.ExecuteAsync(null).ConfigureAwait(true);

        _ = command.CanExecute(null).Should().BeFalse();
        _ = dismissed.Should().BeTrue();
        harness.Commands.Verify(value => value.DeleteItemsAsync(
            It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<IReadOnlyList<Guid>>()), Times.Never);
    });

    [TestMethod]
    public Task ContextMenu_SelectedAnchor_PreservesBatchAndDeletesAllCapturedNodes() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(second);
        _ = harness.Commands.Setup(value => value.DeleteItemsAsync(
            It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<IReadOnlyList<Guid>>()))
            .ReturnsAsync(SceneCommandResult.Success);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id, second.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);

        _ = await explorer.PrepareContextMenuAsync(row).ConfigureAwait(true);
        var menu = explorer.BuildContextMenuSource(row!);
        await GetMenuCommand(menu, "Delete selected items").ExecuteAsync(null).ConfigureAwait(true);

        harness.Commands.Verify(value => value.DeleteItemsAsync(
            It.IsAny<SceneDocumentCommandContext>(),
            It.Is<IReadOnlyList<Guid>>(ids => ids.Count == 2 && ids.Contains(node.Id) && ids.Contains(second.Id)),
            It.Is<IReadOnlyList<Guid>>(ids => ids.Count == 0)), Times.Once);
        _ = menu.Items.Select(item => item.Text).Should().NotContain("Rename");
    });

    [TestMethod]
    public Task ContextMenu_Background_DoesNotInheritSelectionEditsOrClearSelection() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);

        _ = await explorer.PrepareContextMenuAsync(null).ConfigureAwait(true);
        var menu = explorer.BuildContextMenuSource(null);

        _ = menu.Items.Where(item => !item.IsSeparator).Select(item => item.Text).Should()
            .Equal("New node", "New folder", "Paste at scene root");
        _ = explorer.SelectedItemsCount.Should().Be(1);
    });

    [TestMethod]
    public Task ContextMenu_UnselectedAnchor_SettlesInspectorAndSelectsExclusively() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(second);
        _ = harness.Commands.Setup(value => value.CompleteEditSessionsAsync(It.IsAny<SceneDocumentCommandContext>(), true))
            .Returns(Task.CompletedTask);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(second.Id).ConfigureAwait(true);

        _ = await explorer.PrepareContextMenuAsync(row).ConfigureAwait(true);

        _ = explorer.SelectedItemsCount.Should().Be(1);
        _ = explorer.SelectedItem.Should().BeSameAs(row);
        harness.Commands.Verify(value => value.CompleteEditSessionsAsync(It.IsAny<SceneDocumentCommandContext>(), true), Times.Once);
    });

    [TestMethod]
    public Task ContextMenu_NonAnchorLocked_DisablesEntireBatchAndMatchesToolbar() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var second = new SceneNode(scene) { Name = "Locked second" };
        scene.RootNodes.Add(second);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id, second.Id]).ConfigureAwait(true);
        var anchor = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        var locked = await explorer.FindAdapterByNodeIdAsync(second.Id).ConfigureAwait(true);
        locked!.IsLocked = true;

        var menu = explorer.BuildContextMenuSource(anchor!);

        _ = GetMenuCommand(menu, "Delete selected items").CanExecute(null).Should().BeFalse();
        _ = explorer.DeleteAction.CanExecute(null).Should().BeFalse();
        _ = GetMenuCommand(menu, "Cut").CanExecute(null).Should().BeFalse();
        _ = explorer.CutAction.CanExecute(null).Should().BeFalse();
        _ = GetMenuCommand(menu, "Copy").CanExecute(null).Should().BeTrue();
        _ = menu.Items.Single(item => item.Text == "Delete selected items").HelpText.Should().Contain("Locked second");
    });

    [TestMethod]
    public Task ContextMenu_PasteAsChild_UsesCapturedNodeAndDistinctDestination() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        _ = harness.Commands.Setup(value => value.DuplicateNodesFromDataAsync(
            It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), node.Id, null, null))
            .ReturnsAsync(SceneCommandResults.Success<IReadOnlyList<SceneNode>>([]));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        await explorer.CopyItemsAsync([row!]).ConfigureAwait(true);

        var menu = explorer.BuildContextMenuSource(row);
        await GetMenuCommand(menu, "Paste as child (keep world pose)").ExecuteAsync(null).ConfigureAwait(true);

        harness.Commands.Verify(value => value.DuplicateNodesFromDataAsync(
            It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), node.Id, null, null), Times.Once);
    });

    [TestMethod]
    public Task ContextMenu_FolderCopyAndPaste_RoutesWholePayloadThroughAtomicOwner() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout(
        [
            new ExplorerEntryData
            {
                Type = "Folder", FolderId = folderId, Name = "Group",
                Children = [new ExplorerEntryData { NodeId = node.Id }],
            },
        ]);
        var payload = new SceneExplorerClipboard(scene.ExplorerLayout!.ToArray(), [node.Dehydrate()]);
        _ = harness.Commands.Setup(value => value.CaptureExplorerClipboard(
            It.IsAny<SceneDocumentCommandContext>(), It.Is<IReadOnlyList<Guid>>(ids => ids.Count == 0),
            It.Is<IReadOnlyList<Guid>>(ids => ids.SequenceEqual(new[] { folderId }))))
            .Returns(SceneCommandResults.Success(payload));
        _ = harness.Commands.Setup(value => value.ValidateExplorerPaste(
            It.IsAny<SceneDocumentCommandContext>(), payload, false, null, null, false)).Returns((string?)null);
        _ = harness.Commands.Setup(value => value.PasteExplorerItemsAsync(
            It.IsAny<SceneDocumentCommandContext>(), payload, false, null, null, false, null))
            .ReturnsAsync(SceneCommandResult.Success);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        var folder = explorer.ShownItems.OfType<FolderAdapter>().Single();
        await explorer.CopyItemsAsync([folder]).ConfigureAwait(true);

        var backgroundMenu = explorer.BuildContextMenuSource(null);
        await GetMenuCommand(backgroundMenu, "Paste at scene root").ExecuteAsync(null).ConfigureAwait(true);

        harness.Commands.Verify(value => value.PasteExplorerItemsAsync(
            It.IsAny<SceneDocumentCommandContext>(), payload, false, null, null, false, null), Times.Once);
        _ = explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Copied);
    });

    [TestMethod]
    public Task ContextMenu_DefaultCutPaste_UsesPreserveLocalIntent() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        _ = harness.Commands.Setup(value => value.ReparentNodesAsync(
            It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), null, false, null))
            .ReturnsAsync(SceneCommandResult.Success);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        await explorer.CutItemsAsync([row!]).ConfigureAwait(true);

        await GetMenuCommand(explorer.BuildContextMenuSource(null), "Paste at scene root").ExecuteAsync(null).ConfigureAwait(true);

        harness.Commands.Verify(value => value.ReparentNodesAsync(
            It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), null, false, null), Times.Once);
    });

    [TestMethod]
    public Task ContextMenu_CapturedDocumentReplaced_RejectsCommandBeforeAuthoring() => EnqueueAsync(async () =>
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var secondScene = harness.AddScene("Replacement");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        var command = GetMenuCommand(explorer.BuildContextMenuSource(row), "Delete node");

        await explorer.HandleDocumentOpenedAsync(secondScene).ConfigureAwait(true);
        await command.ExecuteAsync(null).ConfigureAwait(true);

        _ = command.CanExecute(null).Should().BeFalse();
        harness.Commands.Verify(value => value.DeleteItemsAsync(
            It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<IReadOnlyList<Guid>>()), Times.Never);
    });

    [TestMethod]
    public Task ContextMenu_NoLoadedDocument_HasNoInertCommands() => EnqueueAsync(() =>
    {
        var harness = new AuthoringHarness(out _, out _);
        using var explorer = harness.Build();

        _ = explorer.BuildContextMenuSource(null).Items.Should().BeEmpty();
        _ = explorer.NewNodeAction.CanExecute(null).Should().BeFalse();
        _ = explorer.NewFolderAction.CanExecute(null).Should().BeFalse();
        return Task.CompletedTask;
    });

    private static IAsyncRelayCommand GetMenuCommand(IMenuSource menu, string label)
        => (IAsyncRelayCommand)menu.Items.Single(item => string.Equals(item.Text, label, StringComparison.Ordinal)).Command!;

    private sealed class AuthoringHarness
    {
        private readonly Scene scene;
        private readonly List<SceneDocumentMetadata> openDocuments = [];

        public AuthoringHarness(out Scene scene, out SceneNode node)
        {
            var project = new Project(new ProjectInfo("Authoring", Category.Games, @"H:\AuthoringTreeTests", "preview.png")) { Name = "Authoring" };
            scene = new Scene(project) { Name = "Scene" };
            node = new SceneNode(scene) { Name = "Node" };
            scene.RootNodes.Add(node);
            project.Scenes.Add(scene);
            this.scene = scene;
            this.Manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = this.Manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Documents = new Mock<IDocumentService>();
            var metadata = new SceneDocumentMetadata(scene.Id);
            this.openDocuments.Add(metadata);
            _ = this.Documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns(() => this.openDocuments);
            _ = this.Documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
            this.Sync = new Mock<ISceneEngineSync>();
            _ = this.Sync.Setup(value => value.GetDocumentScene(metadata)).Returns(scene);
            _ = this.Sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata)).Returns(true);
            _ = this.Sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(false);
            this.Commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        }

        public Mock<IProjectManagerService> Manager { get; }

        public Mock<IDocumentService> Documents { get; }

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public Scene AddScene(string name)
        {
            var added = new Scene(this.scene.Project) { Name = name };
            this.scene.Project.Scenes.Add(added);
            var metadata = new SceneDocumentMetadata(added.Id);
            this.openDocuments.Add(metadata);
            _ = this.Sync.Setup(value => value.GetDocumentScene(metadata)).Returns(added);
            _ = this.Sync.Setup(value => value.RegisterDocument(added, metadata)).Returns(true);
            return added;
        }

        public SceneExplorerViewModel Build()
            => new(this.Manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), this.Documents.Object,
                default, this.Sync.Object, new SceneSelectionService(), this.Commands.Object);
    }
}

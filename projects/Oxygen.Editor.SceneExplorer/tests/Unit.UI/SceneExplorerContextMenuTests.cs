// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneExplorer.Unit.UI.Tests;

/// <summary>Verifies captured context-menu targets without physical input or focus dependencies.</summary>
[TestClass]
internal sealed class SceneExplorerContextMenuTests : DroidNet.Tests.VisualUserInterfaceTests
{
    [TestMethod]
    public Task ContextMenuSelectionChangesRejectsCapturedDelete() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node, static (built, _) => built.RootNodes.Add(new SceneNode(built, "Second")));
        var second = scene.RootNodes.Single(static candidate => candidate.Metadata.Name == "Second");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        var menu = explorer.BuildContextMenuSource(row!);
        var command = GetMenuCommand(menu, "Delete node");
        var dismissed = false;
        explorer.ContextMenuInvalidated += (_, _) => dismissed = true;

        _ = await explorer.SetSelectedNodes([second.Id]).ConfigureAwait(true);
        await command.ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = command.CanExecute(parameter: null).Should().BeFalse();
        _ = dismissed.Should().BeTrue();
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = harness.History.UndoStack.Should().BeEmpty();
    });

    [TestMethod]
    public Task ContextMenuSelectedAnchorPreservesBatchAndDeletesAllCapturedNodes() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node, static (built, _) => built.RootNodes.Add(new SceneNode(built, "Second")));
        var second = scene.RootNodes.Single(static candidate => candidate.Metadata.Name == "Second");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id, second.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);

        _ = await explorer.PrepareContextMenuAsync(row).ConfigureAwait(true);
        var menu = explorer.BuildContextMenuSource(row!);
        await GetMenuCommand(menu, "Delete selected items").ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = scene.RootNodes.Should().BeEmpty();
        _ = harness.History.UndoStack.Should().ContainSingle();
        _ = menu.Items.Select(item => item.Text).Should().NotContain("Rename");
    });

    [TestMethod]
    public Task ContextMenuBackgroundDoesNotInheritSelectionEditsOrClearSelection() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);

        _ = await explorer.PrepareContextMenuAsync(anchor: null).ConfigureAwait(true);
        var menu = explorer.BuildContextMenuSource(anchor: null);

        _ = menu.Items.Where(item => !item.IsSeparator).Select(item => item.Text).Should()
            .Equal("New node", "New folder", "Paste at scene root");
        _ = explorer.SelectedItemsCount.Should().Be(1);
    });

    [TestMethod]
    public Task ContextMenuUnselectedAnchorSettlesInspectorAndSelectsExclusively() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node, static (built, _) => built.RootNodes.Add(new SceneNode(built, "Second")));
        var second = scene.RootNodes.Single(static candidate => candidate.Metadata.Name == "Second");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(second.Id).ConfigureAwait(true);

        _ = await explorer.PrepareContextMenuAsync(row).ConfigureAwait(true);

        _ = explorer.SelectedItemsCount.Should().Be(1);
        _ = explorer.SelectedItem.Should().BeSameAs(row);
    });

    [TestMethod]
    public Task ContextMenuNonAnchorLockedDisablesEntireBatchAndMatchesToolbar() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node, static (built, _) => built.RootNodes.Add(new SceneNode(built, "Locked second")));
        var second = scene.RootNodes.Single(static candidate => candidate.Metadata.Name == "Locked second");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id, second.Id]).ConfigureAwait(true);
        var anchor = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        var locked = await explorer.FindAdapterByNodeIdAsync(second.Id).ConfigureAwait(true);
        locked!.IsLocked = true;

        var menu = explorer.BuildContextMenuSource(anchor!);

        _ = GetMenuCommand(menu, "Delete selected items").CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.DeleteAction.CanExecute(parameter: null).Should().BeFalse();
        _ = GetMenuCommand(menu, "Cut").CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.CutAction.CanExecute(parameter: null).Should().BeFalse();
        _ = GetMenuCommand(menu, "Copy").CanExecute(parameter: null).Should().BeTrue();
        _ = menu.Items.Single(item => string.Equals(item.Text, "Delete selected items", StringComparison.Ordinal)).HelpText.Should().Contain("Locked second");
    });

    [TestMethod]
    public Task ContextMenuPasteAsChildUsesCapturedNodeAndDistinctDestination() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        await explorer.CopyItemsAsync([row!]).ConfigureAwait(true);

        var menu = explorer.BuildContextMenuSource(row);
        await GetMenuCommand(menu, "Paste as child (keep world pose)").ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = node.Hierarchy.Children.Should().ContainSingle().Which.Metadata.Name.Should().Be(node.Metadata.Name);
        _ = harness.History.UndoStack.Should().ContainSingle();
    });

    [TestMethod]
    public Task ContextMenuFolderCopyAndPaste_PastesTheFolderWithItsNodeInOneUndoStep() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out _, layout: static node =>
        [
            new ExplorerEntryData
            {
                Type = "Folder", FolderId = FolderId.New(), Name = "Group",
                Children = [new ExplorerEntryData { Type = "Node", NodeId = node.Id }],
            },
        ]);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        var folder = explorer.ShownItems.OfType<FolderAdapter>().Single();
        await explorer.CopyItemsAsync([folder]).ConfigureAwait(true);

        var backgroundMenu = explorer.BuildContextMenuSource(anchor: null);
        await GetMenuCommand(backgroundMenu, "Paste at scene root").ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = harness.Layout.Where(static entry => entry.Name == "Group").Should().HaveCount(2);
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = harness.History.UndoStack.Should().ContainSingle();
        _ = explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Copied);
    });

    [TestMethod]
    public Task ContextMenuDefaultCutPasteKeepsTheLocalTransform() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var parent, static (built, root) =>
        {
            root.Transform.LocalPosition = new System.Numerics.Vector3(10, 0, 0);
            var child = new SceneNode(built, "Child");
            child.Transform.LocalPosition = new System.Numerics.Vector3(1, 0, 0);
            root.Hierarchy.AddChild(child);
        });
        var node = parent.Hierarchy.Children.Single();
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        await explorer.CutItemsAsync([row!]).ConfigureAwait(true);

        await GetMenuCommand(explorer.BuildContextMenuSource(anchor: null), "Paste at scene root").ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = node.Hierarchy.Parent.Should().BeNull();
        _ = node.Transform.LocalPosition.Should().Be(new System.Numerics.Vector3(1, 0, 0));
    });

    [TestMethod]
    public Task ContextMenuCapturedDocumentReplacedRejectsCommandBeforeAuthoring() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node);
        var secondScene = harness.AddScene("Replacement");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);
        var command = GetMenuCommand(explorer.BuildContextMenuSource(row), "Delete node");

        await explorer.HandleDocumentOpenedAsync(secondScene).ConfigureAwait(true);
        await command.ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = command.CanExecute(parameter: null).Should().BeFalse();
        _ = scene.RootNodes.Should().ContainSingle();
        _ = harness.History.UndoStack.Should().BeEmpty();
    });

    [TestMethod]
    public Task ContextMenuNoLoadedDocumentHasNoInertCommands() => EnqueueAsync(() =>
    {
        using var harness = new AuthoringHarness(out _, out _);
        using var explorer = harness.Build();

        _ = explorer.BuildContextMenuSource(anchor: null).Items.Should().BeEmpty();
        _ = explorer.NewNodeAction.CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.NewFolderAction.CanExecute(parameter: null).Should().BeFalse();
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task CameraNodeMenuSendsViewportCameraActionsToTheSceneEditor() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node);
        node.Components.Add(new PerspectiveCamera { Name = "Camera" });
        var requests = new List<(NodeId Node, SceneCameraCommand Command)>();
        var views = new Mock<ISceneViews>();
        _ = views.Setup(value => value.CameraStateOf(harness.DocumentId)).Returns(new SceneCameraViewState(ViewedCameraId: null, IsPiloting: false));
        _ = views.Setup(value => value.RunCameraCommandAsync(harness.DocumentId, It.IsAny<NodeId>(), It.IsAny<SceneCameraCommand>()))
            .Callback((DocumentId _, NodeId camera, SceneCameraCommand command) => requests.Add((camera, command)))
            .ReturnsAsync(true);
        using var explorer = harness.Build(views.Object);
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);

        var menu = explorer.BuildContextMenuSource(row!);
        _ = menu.Items.Where(item => item.Text is "Look through camera" or "Pilot camera" or "Align camera to view")
            .Should().HaveCount(3).And.OnlyContain(item => item.IsEnabled);
        await GetMenuCommand(menu, "Pilot camera").ExecuteAsync(parameter: null).ConfigureAwait(true);
        await GetMenuCommand(menu, "Align camera to view").ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = requests.Should().Equal((node.Id, SceneCameraCommand.Pilot), (node.Id, SceneCameraCommand.AlignToView));
    });

    [TestMethod]
    public Task CameraNodeMenuReflectsThePilotedViewport() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node);
        node.Components.Add(new OrthographicCamera { Name = "Camera" });
        var views = new Mock<ISceneViews>();
        _ = views.Setup(value => value.CameraStateOf(harness.DocumentId)).Returns(new SceneCameraViewState(node.Id, IsPiloting: true));
        using var explorer = harness.Build(views.Object);
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);

        var menu = explorer.BuildContextMenuSource(row!);

        _ = menu.Items.Select(static item => item.Text).Should().Contain(["Return to editor camera", "Stop piloting camera"]);
        var align = menu.Items.Single(static item => item.Text == "Align camera to view");
        _ = align.IsEnabled.Should().BeFalse();
        _ = align.HelpText.Should().Contain("return it to the editor camera");
    });

    [TestMethod]
    public Task CameraNodeMenuNeedsASceneViewport() => EnqueueAsync(async () =>
    {
        using var harness = new AuthoringHarness(out var scene, out var node, static (built, _) => built.RootNodes.Add(new SceneNode(built, "Plain")));
        node.Components.Add(new PerspectiveCamera { Name = "Camera" });
        var plain = scene.RootNodes.Single(static candidate => candidate.Metadata.Name == "Plain");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(true);
        var row = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(true);

        var menu = explorer.BuildContextMenuSource(row!);

        _ = menu.Items.Single(static item => item.Text == "Look through camera").HelpText.Should().Be("Open the scene in a viewport first.");
        _ = await explorer.SetSelectedNodes([plain.Id]).ConfigureAwait(true);
        var plainRow = await explorer.FindAdapterByNodeIdAsync(plain.Id).ConfigureAwait(true);
        _ = explorer.BuildContextMenuSource(plainRow!).Items.Select(static item => item.Text).Should()
            .NotContain(["Look through camera", "Pilot camera", "Align camera to view"]);
    });

    private static IAsyncRelayCommand GetMenuCommand(IMenuSource menu, string label)
        => (IAsyncRelayCommand)menu.Items.Single(item => string.Equals(item.Text, label, StringComparison.Ordinal)).Command!;

    private sealed class AuthoringHarness : IDisposable
    {
        private readonly Scene scene;
        private readonly List<TestScene> openDocuments = [];

        public AuthoringHarness(out Scene scene, out SceneNode node, Action<Scene, SceneNode>? build = null, Func<SceneNode, IList<ExplorerEntryData>>? layout = null)
        {
            var built = new Scene { Name = "Scene" };
            var builtNode = new SceneNode(built, "Node");
            built.RootNodes.Add(builtNode);
            build?.Invoke(built, builtNode);
            this.Documents = new TestDocumentArea();
            var metadata = TestScenes.Open(built, layout: layout?.Invoke(builtNode));
            scene = metadata.Scene;
            node = metadata.NodeOf(builtNode);
            this.scene = scene;
            this.openDocuments.Add(metadata);
            this.Documents.Activate(metadata.Document);
        }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Oxygen.Editor.Documents.OpenDocuments OpenDocuments => this.openDocuments[0].Documents;
        /// <summary>Gets the history of the harness's first scene.</summary>
        public DroidNet.TimeMachine.HistoryKeeper History => this.openDocuments[0].Document.History;

        /// <summary>Gets the explorer layout of the harness's first scene.</summary>
        public IList<ExplorerEntryData> Layout => this.openDocuments[0].Layout;

        /// <summary>Gets the identity of the harness's first scene document.</summary>
        public DocumentId DocumentId => this.openDocuments[0].DocumentId;

        public void Dispose()
        {
            foreach (var testScene in this.openDocuments)
            {
                testScene.Dispose();
            }
        }

        public Scene AddScene(string name)
        {
            var added = new Scene { Name = name };
            var metadata = TestScenes.Open(added);
            added = metadata.Scene;
            this.openDocuments.Add(metadata);
            this.Documents.Add(metadata.Document);
            return added;
        }

        public SceneExplorerViewModel Build(ISceneViews? views = null)
            => new(Mock.Of<IRouter>(), this.Documents,
                this.OpenDocuments,
                new SceneSelectionService(),
                views: views);
    }
}

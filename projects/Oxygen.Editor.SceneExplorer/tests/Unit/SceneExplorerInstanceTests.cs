// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>
/// An instance's nodes in the Scene Explorer (scene instances 6.2, 6.9, 8.4): an instance root's row holds the rows of
/// the nodes its source brings in, dimmed; selection follows them by node reference both ways; <em>Select instance</em>
/// selects the instance root; Eye and Lock key them by reference; structural commands are refused with the instance
/// rule; reading a structure drops the entries of nodes it no longer holds.
/// </summary>
[TestClass]
public sealed class SceneExplorerInstanceTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");

    [TestMethod]
    public async Task AnInstanceRoot_HoldsTheRowsOfItsSourcesNodes()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);

        var root = (SceneNodeAdapter)(await explorer.FindAdapterByNodeIdAsync(harness.Root).ConfigureAwait(false))!;
        _ = root.IsInstanceRoot.Should().BeTrue();
        _ = root.Annotation.Should().Be("Car.glb", "the instance root names its source");
        _ = root.IsDimmed.Should().BeFalse("the scene owns its instance root");
        var body = root.CurrentChildren.OfType<InstancedNodeAdapter>().Should().ContainSingle().Subject;
        _ = body.Reference.Should().Be(harness.Body);
        _ = body.Label.Should().Be("Body");
        _ = body.IsDimmed.Should().BeTrue("the scene does not own the nodes an instance brings in");
        var door = body.CurrentChildren.OfType<InstancedNodeAdapter>().Should().ContainSingle().Subject;
        _ = door.Reference.Should().Be(harness.Door);
    }

    [TestMethod]
    public async Task ClickingAnInstancedRow_SelectsItsNodeByReference()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        var body = await harness.RevealAsync(explorer, harness.Body).ConfigureAwait(false);

        explorer.SelectDisplayedItem(body, [.. explorer.ShownItems], isControlDown: false, isShiftDown: false);

        var context = harness.SelectionService.GetContext(harness.Document);
        _ = context.Kind.Should().Be(SceneSelectionKind.Node);
        _ = context.SelectedNodes.Should().Equal([harness.Body]);
        _ = context.PrimaryNode.Should().Be(harness.Body);
    }

    [TestMethod]
    public async Task AViewportPickOfAnInstancedNode_SelectsItsRow()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);

        harness.SelectionService.Publish(harness.Document, SceneSelectionContext.ForNodes([harness.Door]), "Viewport");

        var door = explorer.SelectedItem.Should().BeOfType<InstancedNodeAdapter>().Subject;
        _ = door.Reference.Should().Be(harness.Door, "the tree expands to the node the viewport double-clicked");
        _ = explorer.ActiveItem.Should().BeSameAs(door);
    }

    [TestMethod]
    public async Task StructuralCommandsOnAnInstancedRow_AreDisabled()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        _ = await harness.RevealAsync(explorer, harness.Door).ConfigureAwait(false);

        _ = explorer.RenameAction.CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.DeleteAction.CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.CutAction.CanExecute(parameter: null).Should().BeFalse();
        _ = explorer.NewNodeAction.CanExecute(parameter: null).Should().BeFalse("a node an instance brought in takes no new child");
        _ = explorer.NewFolderAction.CanExecute(parameter: null).Should().BeFalse();
    }

    [TestMethod]
    public async Task DeletingASelectionHoldingAnInstancedNode_IsRefusedWhole()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        harness.SelectionService.Publish(harness.Document, SceneSelectionContext.ForNodes([NodeReference.To(harness.Root), harness.Door]), "Viewport");

        _ = explorer.DeleteAction.CanExecute(parameter: null).Should().BeFalse();
        await explorer.RemoveSelectedItems().ConfigureAwait(false);

        _ = harness.Scene.AllNodes.Should().Contain(node => node.Id == harness.Root, "no batch applies to part of the selection");
    }

    [TestMethod]
    public async Task DraggingAnInstancedRow_IsRejected()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        var door = await harness.RevealAsync(explorer, harness.Door).ConfigureAwait(false);

        var result = await explorer.CommitDropAsync(new TreeDropRequest([door], explorer.Scene!, 0, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Should().Be(TreeDropResult.Rejected);
    }

    [TestMethod]
    public async Task TheEyeOfAnInstancedRow_HidesItsNodeByReference()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        var body = await harness.RevealAsync(explorer, harness.Body).ConfigureAwait(false);

        await explorer.ToggleEditorHiddenCommand.ExecuteAsync(body).ConfigureAwait(false);

        _ = harness.Interaction.IsHidden(harness.Body).Should().BeTrue();
        _ = harness.Interaction.IsHidden(NodeReference.To(harness.Root)).Should().BeFalse("the instance root is not hidden with its node");
        _ = body.IsHiddenInEditor.Should().BeTrue();
        var door = (InstancedNodeAdapter)explorer.RowsOf([harness.Door]).Single();
        _ = door.IsEffectivelyHiddenInEditor.Should().BeTrue("a node below a hidden instanced node is hidden with it");
        _ = door.IsHiddenInEditor.Should().BeFalse();
    }

    [TestMethod]
    public async Task TheLockOfAnInstancedRow_LocksItsNodeByReference()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        var door = await harness.RevealAsync(explorer, harness.Door).ConfigureAwait(false);

        explorer.ToggleEditorLockedCommand.Execute(door);

        _ = harness.Interaction.IsLocked(harness.Door).Should().BeTrue();
        _ = door.IsLocked.Should().BeTrue();
    }

    [TestMethod]
    public async Task ReadingAStructure_DropsTheEntriesOfNodesItNoLongerHolds()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        var gone = new NodeReference(harness.Root, NodePath.Of(NodeId.New()));
        harness.Interaction.SetLocked(gone, isLocked: true);
        harness.Interaction.SetHidden(gone, isHidden: true);
        harness.Interaction.SetLocked(harness.Door, isLocked: true);

        // Any edit the Explorer reconciles reads the structures again.
        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = harness.Interaction.IsLocked(gone).Should().BeFalse("the source no longer holds the node");
        _ = harness.Interaction.IsHidden(gone).Should().BeFalse();
        _ = harness.Interaction.IsLocked(harness.Door).Should().BeTrue();
    }

    [TestMethod]
    public async Task ASourceNotCooked_ShowsNoInstancedRows()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        harness.Structures.Withdraw(Car);

        using var explorer = await harness.OpenAsync().ConfigureAwait(false);

        var root = (SceneNodeAdapter)(await explorer.FindAdapterByNodeIdAsync(harness.Root).ConfigureAwait(false))!;
        _ = root.CurrentChildren.Should().BeEmpty();
    }

    /// <summary>A scene with an instance of <c>Car.glb</c>, whose structure holds <c>Body</c> and its child <c>Door_FL</c>.</summary>
    private sealed class InstanceHarness : IDisposable
    {
        private readonly TestSession session;
        private readonly TestScene testScene;

        private InstanceHarness(TestSession session, TestScene testScene, SceneNodeInteraction interaction, NodeId root)
        {
            this.session = session;
            this.testScene = testScene;
            this.Interaction = interaction;
            this.Root = root;
            this.Body = new NodeReference(root, NodePath.Of(NodeId.New()));
            this.Door = new NodeReference(root, NodePath.Of([.. this.Body.Path.Nodes, NodeId.New()]));
            _ = this.Structures.Publish(Car, (this.Body.Path, "Body", null), (this.Door.Path, "Door_FL", this.Body.Path));
            this.Documents.Activate(testScene.Document);
        }

        public Scene Scene => this.testScene.Scene;

        public DocumentId Document => this.testScene.DocumentId;

        public NodeId Root { get; }

        public NodeReference Body { get; }

        public NodeReference Door { get; }

        public FakeSceneStructures Structures { get; } = new();

        public SceneSelectionService SelectionService { get; } = new();

        public SceneNodeInteraction Interaction { get; }

        private TestDocumentArea Documents { get; } = new();

        public static async Task<InstanceHarness> CreateAsync()
        {
            var project = TestProject.Create(new MockFileSystem(), @"C:\InstanceHarness");
            var session = new TestSession(project.Context);
            var interaction = new SceneNodeInteraction(session, Mock.Of<IEditorSettingsManager>(), Mock.Of<IOperationResultPublisher>(), Mock.Of<IStatusReducer>());
            var testScene = TestScenes.Open(new Scene { Name = "Harbor" }, interaction, project: project);
            var node = SceneNodeTemplates.Instance(testScene.Scene, Car);
            var result = await testScene.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(testScene.Scene, testScene.Layout, node)])).ConfigureAwait(false);
            _ = result.Succeeded.Should().BeTrue();
            return new InstanceHarness(session, testScene, interaction, node.Id);
        }

        public async Task<SceneExplorerViewModel> OpenAsync()
        {
            var explorer = new SceneExplorerViewModel(
                Mock.Of<IRouter>(),
                this.Documents,
                this.testScene.Documents,
                this.SelectionService,
                interaction: this.Interaction,
                session: this.session,
                structures: this.Structures);
            await explorer.HandleDocumentOpenedAsync(this.Scene).ConfigureAwait(false);
            return explorer;
        }

        /// <summary>Selects a node through the viewport, which expands the tree to its row, and returns the row.</summary>
        public async Task<InstancedNodeAdapter> RevealAsync(SceneExplorerViewModel explorer, NodeReference node)
        {
            this.SelectionService.Publish(this.Document, SceneSelectionContext.ForNodes([node]), "Viewport");
            await Task.Yield();
            return (InstancedNodeAdapter)explorer.RowsOf([node]).Single();
        }

        public void Dispose()
        {
            this.testScene.Dispose();
            this.session.Dispose();
        }
    }
}

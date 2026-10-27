// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>
/// An instance's nodes in the Scene Explorer (scene instances 6.2, 6.9, 8.4): an instance root's row holds the rows of
/// the nodes its source brings in, dimmed; selection follows them by node reference both ways; <em>Select instance</em>
/// selects the instance root; Eye and Lock key them by reference; structural commands are refused with the instance
/// rule; reading a structure drops the entries of nodes it no longer holds.
/// </summary>
[TestClass]
public sealed partial class SceneExplorerInstanceTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath Van = AssetPath.Parse("/Content/Vehicles/Van.glb");

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
    public async Task AnInstancedRow_StaysHiddenAndLockedThroughItsInstancedParent()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        _ = await harness.RevealAsync(explorer, harness.Door).ConfigureAwait(false);
        harness.Interaction.SetHidden(harness.Door, isHidden: true);
        harness.Interaction.SetLocked(harness.Door, isLocked: true);
        harness.Interaction.SetHidden(harness.Body, isHidden: true);
        harness.Interaction.SetLocked(harness.Body, isLocked: true);
        harness.SelectionService.Publish(harness.Document, SceneSelectionContext.ForNodes([harness.Door]), "Viewport");

        _ = explorer.GetActionCommand(SceneExplorerCommandKind.Show).CanExecute(parameter: null).Should().BeFalse("the door stays hidden with the body");
        _ = explorer.GetActionCommand(SceneExplorerCommandKind.Unlock).CanExecute(parameter: null).Should().BeFalse("the door stays locked with the body");
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

        // Structures read again, as after a cook, drop the entries of nodes their sources no longer hold.
        harness.Structures.NotifyChanged();

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

    [TestMethod]
    public async Task SwappingTheSource_ShowsTheNewSourcesRows()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        var chassis = NodePath.Of(NodeId.New());
        _ = harness.Structures.Publish(Van, (chassis, "Chassis", null));
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);

        var result = await harness.Authoring.ExecuteAsync(new SetInstanceSource(harness.Root, Van, InstanceSwap.KeepByName)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var root = (SceneNodeAdapter)(await explorer.FindAdapterByNodeIdAsync(harness.Root).ConfigureAwait(false))!;
        _ = root.Annotation.Should().Be("Van.glb", "the instance root names its new source");
        _ = root.CurrentChildren.OfType<InstancedNodeAdapter>().Should().ContainSingle()
            .Which.Reference.Should().Be(new NodeReference(harness.Root, chassis));
    }

    [TestMethod]
    public async Task AnInstanceRootWithKeptChanges_CarriesTheCautionMark_UntilTheyAreRemoved()
    {
        using var harness = await InstanceHarness.CreateAsync().ConfigureAwait(false);
        using var explorer = await harness.OpenAsync().ConfigureAwait(false);
        var root = (SceneNodeAdapter)(await explorer.FindAdapterByNodeIdAsync(harness.Root).ConfigureAwait(false))!;
        _ = (await ChangeAsync(harness, harness.Body.Path).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = root.HasKeptChanges.Should().BeFalse("Car.glb holds Body");

        // Car.glb reimported without Body: the next change of the instance shows Body's change kept.
        _ = harness.Structures.Publish(Car, (harness.Door.Path, "Door_FL", null));
        _ = (await ChangeAsync(harness, harness.Door.Path).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = root.HasKeptChanges.Should().BeTrue("Car.glb holds no node with the path of Body's change");
        _ = root.ShowsNodeGlyph.Should().BeFalse("the caution glyph takes the node glyph's place");
        _ = (await harness.Authoring.ExecuteAsync(new RemoveKeptChanges(harness.Root, [harness.Body.Path])).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = root.HasKeptChanges.Should().BeFalse();
    }

    private static Task<SceneEditResult> ChangeAsync(InstanceHarness harness, NodePath path)
        => harness.Authoring.ExecuteAsync(new SetOverrides(
            new Dictionary<NodeId, IReadOnlyList<OverrideWrite>> { [harness.Root] = [new OverrideWrite(SceneSchema.Transform.PositionX.Id, Part: null, LayoutRevision: null, 1f) { Path = path }] },
            "Change"));

    /// <summary>A scene with an instance of <c>Car.glb</c>, whose structure holds <c>Body</c> and its child <c>Door_FL</c>.</summary>
    private sealed partial class InstanceHarness : IDisposable
    {
        private readonly TestSession session;
        private readonly TestScene testScene;

        private InstanceHarness(TestSession session, TestScene testScene, SceneNodeInteraction interaction, FakeSceneStructures structures, NodeId root)
        {
            this.session = session;
            this.testScene = testScene;
            this.Interaction = interaction;
            this.Structures = structures;
            this.Root = root;
            this.Body = new NodeReference(root, NodePath.Of(NodeId.New()));

            // As a cooked source holds them: the door's parent is the body in the contents, not in its path.
            this.Door = new NodeReference(root, NodePath.Of(NodeId.New()));
            _ = this.Structures.Publish(Car, (this.Body.Path, "Body", null), (this.Door.Path, "Door_FL", this.Body.Path));
            this.Documents.Activate(testScene.Document);
        }

        public Scene Scene => this.testScene.Scene;

        public SceneAuthoring Authoring => this.testScene.Authoring;

        public DocumentId Document => this.testScene.DocumentId;

        public NodeId Root { get; }

        public NodeReference Body { get; }

        public NodeReference Door { get; }

        public FakeSceneStructures Structures { get; }

        public SceneSelectionService SelectionService { get; } = new();

        public SceneNodeInteraction Interaction { get; }

        private TestDocumentArea Documents { get; } = new();

        public static async Task<InstanceHarness> CreateAsync()
        {
            var project = TestProject.Create(new MockFileSystem(), @"C:\InstanceHarness");
            var projectSession = new TestSession(project.Context);
            var structures = new FakeSceneStructures();
            var interaction = new SceneInteractionKit(structures).Create(projectSession);
            var opened = TestScenes.Open(new Scene { Name = "Harbor" }, interaction, project: project, structures: structures);
            var node = SceneNodeTemplates.Instance(opened.Scene, Car);
            var result = await opened.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(opened.Scene, opened.Layout, node)])).ConfigureAwait(false);
            _ = result.Succeeded.Should().BeTrue();
            return new InstanceHarness(projectSession, opened, interaction, structures, node.Id);
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

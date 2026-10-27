// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneExplorer.Unit.UI.Tests;

/// <summary>
/// The row menu of a node an instance brought in (scene instances 6.2, 6.8, 6.9, 8.4): <em>Select instance</em> selects
/// the instance root, <em>Copy</em> stages an owned copy, every structural command stays in the menu, disabled, with
/// the refusal as its tooltip; the instance root's menu unpacks it; a changed node of an authored scene's instance offers
/// <em>Apply to</em> that scene, which a model's never does.
/// </summary>
[TestClass]
internal sealed partial class SceneExplorerInstanceMenuTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath Watchtower = AssetPath.Parse("/Content/Prefabs/Watchtower.oscene");

    // The row menu's commands that change structure, which a node an instance brings in refuses.
    private static readonly string[] StructuralCommands = ["Rename", "Delete node", "Cut", "New child node", "Paste"];

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task SelectInstance_SelectsTheInstanceRoot() => EnqueueAsync(async () =>
    {
        using var harness = await InstanceMenuHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        var door = harness.Reveal(explorer);

        var item = MenuItem(explorer, door, "Select instance");
        _ = item.IsEnabled.Should().BeTrue();
        await ((IAsyncRelayCommand)item.Command!).ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = harness.Selection.GetContext(harness.Document.DocumentId).SelectedNodes.Should().Equal([NodeReference.To(harness.Root)]);
    });

    [TestMethod]
    public Task StructuralCommands_StayInTheMenuDisabledWithTheReason() => EnqueueAsync(async () =>
    {
        using var harness = await InstanceMenuHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        var door = harness.Reveal(explorer);

        foreach (var label in StructuralCommands)
        {
            var item = MenuItem(explorer, door, label);
            _ = item.IsEnabled.Should().BeFalse(label);
            _ = item.HelpText.Should().NotBeNullOrEmpty($"{label} says why it is refused");
        }
    });

    [TestMethod]
    public Task Copy_StagesAnOwnedCopyOfTheInstancedNode() => EnqueueAsync(async () =>
    {
        using var harness = await InstanceMenuHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        var door = harness.Reveal(explorer);

        var item = MenuItem(explorer, door, "Copy");
        _ = item.IsEnabled.Should().BeTrue();
        await ((IAsyncRelayCommand)item.Command!).ExecuteAsync(parameter: null).ConfigureAwait(true);

        var copy = harness.Clipboard.Payload!.Nodes.Should().ContainSingle().Subject;
        _ = copy.Name.Should().Be("Door_FL");
        _ = copy.Id.Should().NotBe(harness.Door.Path.Nodes[0]);
    });

    [TestMethod]
    public Task Unpack_MakesTheInstancesNodesTheScenesOwn() => EnqueueAsync(async () =>
    {
        using var harness = await InstanceMenuHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        harness.Selection.Publish(harness.Document.DocumentId, SceneSelectionContext.ForNodes([NodeReference.To(harness.Root)]), "Viewport");
        var root = explorer.RowsOf([NodeReference.To(harness.Root)]).Single();

        var item = MenuItem(explorer, root, "Unpack");
        _ = item.IsEnabled.Should().BeTrue();
        _ = MenuItem(explorer, root, "Unpack completely").IsEnabled.Should().BeTrue();
        await ((IAsyncRelayCommand)item.Command!).ExecuteAsync(parameter: null).ConfigureAwait(true);

        var node = harness.Document.Scene.AllNodes.Single(node => node.Id == harness.Root);
        _ = node.Components.OfType<SceneInstanceComponent>().Should().BeEmpty();
        _ = node.Hierarchy.Children.Select(static child => child.Metadata.Name).Should().Equal("Door_FL");
    });

    [TestMethod]
    public Task ApplyTo_MovesAChangedNodesChangeIntoTheAuthoredScene() => EnqueueAsync(async () =>
    {
        using var harness = await InstanceMenuHarness.CreateAsync(Watchtower).ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        harness.ChangeDoor(4f);
        var door = harness.Reveal(explorer);

        _ = await explorer.PrepareContextMenuAsync(door).ConfigureAwait(true);
        var item = MenuItem(explorer, door, "Apply to Watchtower");
        _ = item.IsEnabled.Should().BeTrue();
        await ((IAsyncRelayCommand)item.Command!).ExecuteAsync(parameter: null).ConfigureAwait(true);

        var instance = harness.Document.Scene.AllNodes.Single(node => node.Id == harness.Root).Components.OfType<SceneInstanceComponent>().Single();
        _ = instance.Changes.IsEmpty.Should().BeTrue();
        var watchtower = await SceneFiles.ReadAsync(harness.Document.Project, Watchtower, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = watchtower!.AllNodes.Single(node => node.Id == harness.Door.Path.Nodes[0]).Transform.LocalPosition.X.Should().Be(4f);
    });

    [TestMethod]
    public Task AModelsNode_OffersNoApplyTo() => EnqueueAsync(async () =>
    {
        using var harness = await InstanceMenuHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        harness.ChangeDoor(4f);
        var door = harness.Reveal(explorer);

        _ = await explorer.PrepareContextMenuAsync(door).ConfigureAwait(true);

        _ = explorer.BuildContextMenuSource(door).Items.Should().NotContain(item => item.Text != null && item.Text.StartsWith("Apply to", StringComparison.Ordinal));
    });

    private static MenuItemData MenuItem(SceneExplorerViewModel explorer, ITreeItem row, string label)
        => explorer.BuildContextMenuSource(row).Items.Single(item => string.Equals(item.Text, label, StringComparison.Ordinal));

    /// <summary>
    /// A scene with an instance of <c>Car.glb</c>, or of <c>Watchtower.oscene</c>, whose structure holds the root node
    /// <c>Door_FL</c>.
    /// </summary>
    private sealed partial class InstanceMenuHarness : IDisposable
    {
        private readonly TestSession session;
        private readonly FakeSceneStructures structures = new();
        private readonly TestDocumentArea documents = new();

        private InstanceMenuHarness(TestSession session, TestScene document, NodeId root, AssetPath source, NodePath door)
        {
            this.session = session;
            this.Document = document;
            this.Root = root;
            this.Door = new NodeReference(root, door);
            _ = this.structures.Publish(source, (this.Door.Path, "Door_FL", null));
            this.structures.PublishValues(source, this.Door.Path, new InstancedNodeValues("Door_FL", Geometry: null, []));
            this.documents.Activate(document.Document);
        }

        public TestScene Document { get; }

        public NodeId Root { get; }

        public NodeReference Door { get; }

        public SceneSelectionService Selection { get; } = new();

        public SceneNodeClipboard Clipboard { get; } = new();

        public static async Task<InstanceMenuHarness> CreateAsync(AssetPath? source = null)
        {
            var project = TestProject.Create(new MockFileSystem(), @"C:\InstanceMenuHarness");
            var door = NodeId.New();
            if (source is { } scene)
            {
                // The authored scene's file holds the door at the origin.
                var watchtower = new Scene { Name = "Watchtower" };
                var doorNode = new SceneNode(watchtower, "Door_FL");
                watchtower.RootNodes.Add(doorNode);
                door = doorNode.Id;
                _ = project.WriteFile(scene.VirtualPath + ".json", JsonSerializer.SerializeToUtf8Bytes(new SceneFileData(watchtower.Dehydrate(), explorerLayout: null), SceneFileJsonContext.Default.SceneFileData));
            }

            var document = TestScenes.Open(new Scene { Name = "Harbor" }, project: project);
            var node = SceneNodeTemplates.Instance(document.Scene, source ?? Car);
            _ = (await document.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(document.Scene, document.Layout, node)])).ConfigureAwait(true))
                .Succeeded.Should().BeTrue();
            return new InstanceMenuHarness(new TestSession(project.Context), document, node.Id, source ?? Car, NodePath.Of(door));
        }

        /// <summary>Moves the door, as the Properties pane does: a change of the instance root.</summary>
        /// <param name="x">The door's position x.</param>
        public void ChangeDoor(float x)
        {
            var instance = this.Document.Scene.AllNodes.Single(node => node.Id == this.Root).Components.OfType<SceneInstanceComponent>().Single();
            instance.Changes = InstanceChanges.None.With(this.Door.Path, InstancedNodeChanges.None.With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, x)));
        }

        public async Task<SceneExplorerViewModel> OpenAsync()
        {
            var explorer = new SceneExplorerViewModel(
                Mock.Of<IRouter>(),
                this.documents,
                this.Document.Documents,
                this.Selection,
                session: this.session,
                clipboard: this.Clipboard,
                structures: this.structures,
                nodes: this.structures);
            await explorer.HandleDocumentOpenedAsync(this.Document.Scene).ConfigureAwait(true);
            return explorer;
        }

        /// <summary>Selects the door through the viewport, which expands the tree to its row, and returns the row.</summary>
        public InstancedNodeAdapter Reveal(SceneExplorerViewModel explorer)
        {
            this.Selection.Publish(this.Document.DocumentId, SceneSelectionContext.ForNodes([this.Door]), "Viewport");
            return (InstancedNodeAdapter)explorer.RowsOf([this.Door]).Single();
        }

        public void Dispose()
        {
            this.Document.Dispose();
            this.session.Dispose();
        }
    }
}

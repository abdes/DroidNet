// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneExplorer.Unit.UI.Tests;

/// <summary>
/// The row menu of a node an instance brought in (scene instances 6.2, 6.9, 8.4): <em>Select instance</em> selects
/// the instance root, and every structural command stays in the menu, disabled, with the refusal as its tooltip.
/// </summary>
[TestClass]
internal sealed class SceneExplorerInstanceMenuTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");

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

        foreach (var label in new[] { "Rename", "Delete node", "Cut", "New child node", "Paste" })
        {
            var item = MenuItem(explorer, door, label);
            _ = item.IsEnabled.Should().BeFalse(label);
            _ = item.HelpText.Should().NotBeNullOrEmpty($"{label} says why it is refused");
        }
    });

    private static MenuItemData MenuItem(SceneExplorerViewModel explorer, InstancedNodeAdapter row, string label)
        => explorer.BuildContextMenuSource(row).Items.Single(item => string.Equals(item.Text, label, StringComparison.Ordinal));

    /// <summary>A scene with an instance of <c>Car.glb</c>, whose structure holds the root node <c>Door_FL</c>.</summary>
    private sealed class InstanceMenuHarness : IDisposable
    {
        private readonly TestSession session;
        private readonly FakeSceneStructures structures = new();
        private readonly TestDocumentArea documents = new();

        private InstanceMenuHarness(TestSession session, TestScene document, NodeId root)
        {
            this.session = session;
            this.Document = document;
            this.Root = root;
            this.Door = new NodeReference(root, NodePath.Of(NodeId.New()));
            _ = this.structures.Publish(Car, (this.Door.Path, "Door_FL", null));
            this.documents.Activate(document.Document);
        }

        public TestScene Document { get; }

        public NodeId Root { get; }

        public NodeReference Door { get; }

        public SceneSelectionService Selection { get; } = new();

        public static async Task<InstanceMenuHarness> CreateAsync()
        {
            var project = TestProject.Create(new MockFileSystem(), @"C:\InstanceMenuHarness");
            var document = TestScenes.Open(new Scene { Name = "Harbor" }, project: project);
            var node = SceneNodeTemplates.Instance(document.Scene, Car);
            _ = (await document.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(document.Scene, document.Layout, node)])).ConfigureAwait(true))
                .Succeeded.Should().BeTrue();
            return new InstanceMenuHarness(new TestSession(project.Context), document, node.Id);
        }

        public async Task<SceneExplorerViewModel> OpenAsync()
        {
            var explorer = new SceneExplorerViewModel(
                Mock.Of<IRouter>(),
                this.documents,
                this.Document.Documents,
                this.Selection,
                session: this.session,
                structures: this.structures);
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

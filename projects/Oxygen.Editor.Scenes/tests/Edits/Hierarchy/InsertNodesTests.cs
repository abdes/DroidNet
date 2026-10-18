// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>
/// What is specific to <see cref="InsertNodes"/> (scene-edits.md 4). Targets, locks, undo and the change stream of
/// every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class InsertNodesTests
{
    public static IEnumerable<TestDataRow<InsertCase>> Places =>
    [
        new(new InsertCase(Parent: null, Folder: null, AtStart: false)) { DisplayName = "RootEnd" },
        new(new InsertCase(Parent: null, Folder: null, AtStart: true)) { DisplayName = "RootStart" },
        new(new InsertCase(SampleScene.A, Folder: null, AtStart: false)) { DisplayName = "UnderA" },
        new(new InsertCase(Parent: null, SampleScene.F, AtStart: false)) { DisplayName = "InFolderF" },
        new(new InsertCase(SampleScene.A, SampleScene.AF, AtStart: false)) { DisplayName = "InFolderAF" },
    ];

    [TestMethod]
    [DynamicData(nameof(Places))]
    public async Task At_InsertsTheNodeThere(InsertCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = NewNode("New");
        var at = SceneAt(fixture, row);
        var layout = LayoutAt(fixture, row);

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([new NodeInsert(node, at, layout)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var inserted = fixture.Node(node.Id);
        _ = (inserted.Parent?.Id).Should().Be(row.Parent);
        _ = SiblingsOf(fixture, row.Parent).IndexOf(inserted).Should().Be(at.Index);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(node.Id)).Should().Be(layout);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodesAdded([node.Id]), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task Subtree_InsertsAllAndRaisesOnlyTheRoot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var leaf = NewNode("Leaf");
        var root = NewNode("Root") with { Children = [leaf] };

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([AtRootEnd(fixture, root)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(leaf.Id).Parent!.Id.Should().Be(root.Id);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodesAdded([root.Id]), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    [DataRow(true, SceneEditCodes.NodeExists, DisplayName = "NodeId")]
    [DataRow(false, SceneEditCodes.ComponentDuplicate, DisplayName = "ComponentId")]
    public async Task ExistingId_IsInvalid(bool reuseNodeId, string code)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = reuseNodeId
            ? NewNode("Clash") with { Id = SampleScene.B }
            : NewNode("Clash") with { Components = [new TransformData { Id = SceneBuilder.TransformId(SampleScene.B), Name = "Transform" }] };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([AtRootEnd(fixture, node)])).ConfigureAwait(false);

        SceneAssert.Invalid(result, code);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task UnderMissingParent_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        var insert = new NodeInsert(NewNode("New"), new NodePosition(missing, 0), LayoutPosition.UnderNode(missing, 0));
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([insert])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task UnderLockedParent_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.A);
        var insert = new NodeInsert(
            NewNode("New"),
            new NodePosition(SampleScene.A, fixture.Node(SampleScene.A).Children.Count),
            LayoutPosition.UnderNode(SampleScene.A, LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.UnderNode(SampleScene.A, 0)).Count));
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([insert])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.A);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task DirectionalLightWithTakenSlot_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var sun = NewNode("Second sun") with
        {
            Components =
            [
                new TransformData { Id = ComponentId.New(), Name = "Transform" },
                new DirectionalLightData { Id = ComponentId.New(), Name = "Light", AtmosphereSlot = AtmosphereLightSlot.Primary },
            ],
        };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([AtRootEnd(fixture, sun)])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AtmosphereSlotOccupied);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task LayoutEntryWithFolders_IsRestoredAsGiven()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = NewNode("Holder");
        var nested = FolderId.New();
        var entry = new ExplorerEntryData
        {
            Type = "NodeValue",
            NodeId = node.Id,
            Children =
            [
                new ExplorerEntryData { Type = "Folder", FolderId = nested, Name = "Nested", IsExpanded = false, Children = [] },
            ],
        };
        var insert = new NodeInsert(node, RootEndAt(fixture), RootEndLayout(fixture), entry);

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([insert])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var folder = LayoutQuery.Folder(fixture.Layout, nested);
        _ = folder.Should().NotBeNull();
        _ = folder!.Name.Should().Be("Nested");
        _ = folder.IsExpanded.Should().BeFalse();
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(nested)).Should().Be(LayoutPosition.UnderNode(node.Id, 0));
    }

    [TestMethod]
    public async Task RenamedAsset_IsRetargeted()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Redirects.Move(SampleScene.CubeGeometry, SampleScene.SphereGeometry);
        var node = NewNode("Box") with
        {
            Components =
            [
                new TransformData { Id = ComponentId.New(), Name = "Transform" },
                new GeometryComponentData { Id = ComponentId.New(), Name = "Geometry", GeometryUri = SampleScene.CubeGeometry.ToString() },
            ],
        };

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([AtRootEnd(fixture, node)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var geometry = fixture.Node(node.Id).Dehydrate().Components.OfType<GeometryComponentData>().Single();
        _ = geometry.GeometryUri.Should().Be(SampleScene.SphereGeometry.ToString());
    }

    // The scene index and the layout index are two positions of the same insert: each is checked with the other valid.
    [TestMethod]
    [DataRow(false, DisplayName = "SceneIndex")]
    [DataRow(true, DisplayName = "LayoutIndex")]
    public async Task PositionOutsideRange_IsInvalid(bool inLayout)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var at = RootEndAt(fixture);
        var layout = RootEndLayout(fixture);
        var insert = inLayout
            ? new NodeInsert(NewNode("New"), at, layout with { Index = layout.Index + 1 })
            : new NodeInsert(NewNode("New"), at with { Index = at.Index + 1 }, layout);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([insert])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PositionOutOfRange);
        SceneAssert.Unchanged(fixture, before);
    }

    private static SceneNodeData NewNode(string name)
        => new()
        {
            Id = NodeId.New(),
            Name = name,
            IsActive = true,
            Components = [new TransformData { Id = ComponentId.New(), Name = "Transform" }],
        };

    private static NodeInsert AtRootEnd(AuthoringFixture fixture, SceneNodeData node)
        => new(node, RootEndAt(fixture), RootEndLayout(fixture));

    private static NodePosition RootEndAt(AuthoringFixture fixture) => new(Parent: null, fixture.Scene.RootNodes.Count);

    private static LayoutPosition RootEndLayout(AuthoringFixture fixture)
        => LayoutPosition.AtRoot(LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.AtRoot(0)).Count);

    private static ObservableCollection<SceneNode> SiblingsOf(AuthoringFixture fixture, NodeId? parent)
        => parent is { } id ? fixture.Node(id).Children : fixture.Scene.RootNodes;

    private static NodePosition SceneAt(AuthoringFixture fixture, InsertCase row)
        => new(row.Parent, row.AtStart ? 0 : SiblingsOf(fixture, row.Parent).Count);

    private static LayoutPosition LayoutAt(AuthoringFixture fixture, InsertCase row)
    {
        var container = row.Folder is { } folder
            ? LayoutPosition.InFolder(folder, 0)
            : row.Parent is { } node ? LayoutPosition.UnderNode(node, 0) : LayoutPosition.AtRoot(0);
        return container with { Index = row.AtStart ? 0 : LayoutScopes.EntriesOf(fixture.Layout, container).Count };
    }
}

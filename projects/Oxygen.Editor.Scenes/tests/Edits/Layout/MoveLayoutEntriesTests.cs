// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>
/// What is specific to <see cref="MoveLayoutEntries"/> (scene-edits.md 4): moving within a scope only regroups.
/// Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class MoveLayoutEntriesTests
{
    public static IEnumerable<TestDataRow<MoveCase>> Moves =>
    [
        Row(
            "BToRootStart",
            [LayoutEntryId.Of(SampleScene.B)],
            LayoutPosition.AtRoot(0),
            LayoutPosition.AtRoot(0),
            [LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.A), LayoutEntryId.Of(SampleScene.F), .. RootAfterF]),
        Row(
            "CamIntoF",
            [LayoutEntryId.Of(SampleScene.Cam)],
            LayoutPosition.InFolder(SampleScene.F, 0),
            LayoutPosition.InFolder(SampleScene.F, 0),
            [LayoutEntryId.Of(SampleScene.Cam), LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G)]),
        Row(
            "GToRoot",
            [LayoutEntryId.Of(SampleScene.G)],
            LayoutPosition.AtRoot(1),
            LayoutPosition.AtRoot(0),
            [LayoutEntryId.Of(SampleScene.A), LayoutEntryId.Of(SampleScene.G), LayoutEntryId.Of(SampleScene.F), .. RootAfterF]),
        Row(
            "A2OutOfAF",
            [LayoutEntryId.Of(SampleScene.A2)],
            LayoutPosition.UnderNode(SampleScene.A, 0),
            LayoutPosition.UnderNode(SampleScene.A, 0),
            [LayoutEntryId.Of(SampleScene.A2), LayoutEntryId.Of(SampleScene.A1), LayoutEntryId.Of(SampleScene.AF)]),
    ];

    public static IEnumerable<TestDataRow<CycleCase>> Cycles =>
    [
        new(new CycleCase(SampleScene.F, SampleScene.F)) { DisplayName = "FIntoF" },
        new(new CycleCase(SampleScene.F, SampleScene.G)) { DisplayName = "FIntoG" },
    ];

    // The sample's root after F: Cam, Ortho, Sun, Moon, Lamp, Spot, Fog, Mesh, Script.
    private static LayoutEntryId[] RootAfterF =>
    [
        LayoutEntryId.Of(SampleScene.Cam),
        LayoutEntryId.Of(SampleScene.Ortho),
        LayoutEntryId.Of(SampleScene.Sun),
        LayoutEntryId.Of(SampleScene.Moon),
        LayoutEntryId.Of(SampleScene.Lamp),
        LayoutEntryId.Of(SampleScene.Spot),
        LayoutEntryId.Of(SampleScene.Fog),
        LayoutEntryId.Of(SampleScene.Mesh),
        LayoutEntryId.Of(SampleScene.Script),
    ];

    [TestMethod]
    [DynamicData(nameof(Moves))]
    public async Task To_MovesWithinTheScope(MoveCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var entriesBefore = LayoutQuery.EntryCount(fixture.Scene);

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries(row.Entries, row.To)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.EntriesOf(fixture.Scene, row.Container).Should().Equal(row.Expected);

        // A move takes entries out of their old place: none is left behind or duplicated.
        _ = LayoutQuery.EntryCount(fixture.Scene).Should().Be(entriesBefore);
    }

    [TestMethod]
    public async Task Undo_PutsEachEntryBackAtItsPosition()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.Cam)], LayoutPosition.InFolder(SampleScene.G, 0));

        _ = await SceneAssert.RoundTripAsync(fixture, edit).ConfigureAwait(false);

        _ = LayoutScopes.PositionOf(fixture.Scene, LayoutEntryId.Of(SampleScene.B)).Should().Be(LayoutPosition.InFolder(SampleScene.F, 0));
        _ = LayoutScopes.PositionOf(fixture.Scene, LayoutEntryId.Of(SampleScene.Cam)).Should().Be(LayoutPosition.AtRoot(2));
    }

    [TestMethod]
    public async Task NodeToAnotherScope_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.A2)], LayoutPosition.AtRoot(0))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.LayoutScopeMismatch);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task FolderWithNodesToAnotherScope_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.AF)], LayoutPosition.AtRoot(0))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.LayoutScopeMismatch);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(Cycles))]
    public async Task FolderIntoItselfOrDescendant_IsInvalid(CycleCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([LayoutEntryId.Of(row.Moved)], LayoutPosition.InFolder(row.Into, 0))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.HierarchyCycle);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task MovedFolder_KeepsItsChildrenAndExpandedState()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.F)], LayoutPosition.AtRoot(0))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.PositionOf(fixture.Scene, LayoutEntryId.Of(SampleScene.F)).Should().Be(LayoutPosition.AtRoot(0));
        _ = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.InFolder(SampleScene.F, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G));
        _ = LayoutQuery.Folder(fixture.Scene, SampleScene.F)!.IsExpanded.Should().BeTrue();
        _ = LayoutQuery.Folder(fixture.Scene, SampleScene.G)!.IsExpanded.Should().BeFalse();
    }

    // Rows are offsets from the root's count: B is not in the root, so a move to the root may use 0..count.
    [TestMethod]
    [DataRow(-1, DisplayName = "Negative")]
    [DataRow(1, DisplayName = "CountPlusOne")]
    public async Task Index_OutsideZeroToCount_IsInvalid(int offsetFromCount)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var count = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0)).Count;
        var index = offsetFromCount < 0 ? offsetFromCount : count + offsetFromCount;
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.B)], LayoutPosition.AtRoot(index))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PositionOutOfRange);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Index_Count_Appends()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var count = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0)).Count;

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.B)], LayoutPosition.AtRoot(count))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.PositionOf(fixture.Scene, LayoutEntryId.Of(SampleScene.B)).Should().Be(LayoutPosition.AtRoot(count));
    }

    [TestMethod]
    public async Task NoEntries_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([], LayoutPosition.AtRoot(0))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NothingToEdit);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task CrossScopeGroup_AsBatch_ReparentsKeepingLocal()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var cam = LayoutEntryId.Of(SampleScene.Cam);
        var batch = new SceneEditBatch(
            "Group",
            [
                new ReparentNodes([SampleScene.Cam], SampleScene.A, preserveWorld: false),
                new MoveLayoutEntries([cam], LayoutPosition.InFolder(SampleScene.AF, 0)),
            ]);

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle();
        _ = fixture.Node(SampleScene.Cam).Parent!.Id.Should().Be(SampleScene.A);
        _ = LocalPosition(fixture, SampleScene.Cam).Should().Be(Vector3.Zero);
        _ = LayoutScopes.PositionOf(fixture.Scene, cam).Should().Be(LayoutPosition.InFolder(SampleScene.AF, 0));

        // The single entry undoes the reparent and the move together.
        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Node(SampleScene.Cam).Parent.Should().BeNull();
        _ = LocalPosition(fixture, SampleScene.Cam).Should().Be(Vector3.Zero);
        _ = LayoutScopes.PositionOf(fixture.Scene, cam).Should().Be(LayoutPosition.AtRoot(2));
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
    }

    [TestMethod]
    public async Task Move_RaisesOnlyLayoutChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.B)], LayoutPosition.AtRoot(0))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.LayoutChanged());
    }

    private static TestDataRow<MoveCase> Row(string name, IReadOnlyList<LayoutEntryId> entries, LayoutPosition to, LayoutPosition container, IReadOnlyList<LayoutEntryId> expected)
        => new(new MoveCase(entries, to, container, expected)) { DisplayName = name };

    private static Vector3 LocalPosition(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<TransformComponent>().Single().LocalPosition;
}

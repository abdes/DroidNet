// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>
/// What is specific to <see cref="ReorderNode"/> (scene-edits.md 4). Targets, locks, undo and the change stream of every
/// edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class ReorderNodeTests
{
    [TestMethod]
    public async Task WithinParent_MovesInGraphAndLayout()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new ReorderNode(SampleScene.A2, new NodePosition(SampleScene.A, 0), LayoutPosition.UnderNode(SampleScene.A, 0));

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.A).Children.Select(static node => node.Id).Should().Equal(SampleScene.A2, SampleScene.A1);
        _ = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.UnderNode(SampleScene.A, 0)).Should().StartWith(LayoutEntryId.Of(SampleScene.A2));
        _ = LayoutScopes.PositionOf(fixture.Scene, LayoutEntryId.Of(SampleScene.A2)).Should().Be(LayoutPosition.UnderNode(SampleScene.A, 0));
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodesPlaced([SampleScene.A2]), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task LockedDescendant_IsAllowed()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.A11);
        var edit = new ReorderNode(SampleScene.A, new NodePosition(Parent: null, 1), LayoutPosition.AtRoot(1));

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.RootNodes.Select(static node => node.Id).ToList().IndexOf(SampleScene.A).Should().Be(1);
    }

    [TestMethod]
    [DataRow(false, DisplayName = "Node")]
    [DataRow(true, DisplayName = "Parent")]
    public async Task LockedNodeOrParent_IsRefused(bool lockParent)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(lockParent ? SampleScene.A : SampleScene.A2);
        var edit = new ReorderNode(SampleScene.A2, new NodePosition(SampleScene.A, 0), LayoutPosition.UnderNode(SampleScene.A, 0));
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        // A locked parent also locks the node under it, so both are reported (one error per bad target).
        NodeId[] locked = lockParent ? [SampleScene.A2, SampleScene.A] : [SampleScene.A2];
        _ = result.Outcome.Should().Be(SceneEditOutcome.Refused);
        _ = result.Diagnostics.Select(static diagnostic => diagnostic.Code).Should().AllBe(SceneEditCodes.TargetLocked);
        _ = result.Diagnostics.Select(static diagnostic => diagnostic.Node).Should().BeEquivalentTo(locked.Cast<NodeId?>());
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task MissingParent_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        var edit = new ReorderNode(SampleScene.Cam, new NodePosition(missing, 0), LayoutPosition.UnderNode(missing, 0));
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task MissingFolder_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new ReorderNode(SampleScene.Cam, new NodePosition(Parent: null, 0), LayoutPosition.InFolder(FolderId.New(), 0));
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.FolderMissing);
        SceneAssert.Unchanged(fixture, before);
    }

    // Each row breaks one of the two positions of the edit and keeps the other valid. The offsets are from the number of
    // places the container holds, counting the moved node where it is still there, so the value is outside [0, count]
    // however the moved node itself is counted.
    [TestMethod]
    [DataRow(false, -1, DisplayName = "SceneIndexNegative")]
    [DataRow(false, 1, DisplayName = "SceneIndexPastCount")]
    [DataRow(true, -1, DisplayName = "LayoutIndexNegative")]
    [DataRow(true, 1, DisplayName = "LayoutIndexPastCount")]
    public async Task Index_OutsideZeroToCount_IsInvalid(bool inLayout, int offsetFromCount)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var siblings = fixture.Node(SampleScene.A).Children.Count;
        var entries = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.UnderNode(SampleScene.A, 0)).Count;
        var count = inLayout ? entries : siblings;
        var bad = offsetFromCount < 0 ? offsetFromCount : count + offsetFromCount;
        var edit = new ReorderNode(
            SampleScene.A2,
            new NodePosition(SampleScene.A, inLayout ? 0 : bad),
            LayoutPosition.UnderNode(SampleScene.A, inLayout ? bad : 0));
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PositionOutOfRange);
        SceneAssert.Unchanged(fixture, before);
    }
}

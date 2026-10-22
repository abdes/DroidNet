// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>
/// What is specific to <see cref="RemoveNodes"/> (scene-edits.md 4). Targets, locks, undo and the change stream of every
/// edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RemoveNodesTests
{
    [TestMethod]
    public async Task A_RemovesTheSubtreeAndRaisesNodesRemoved()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveNodes([SampleScene.A])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.AllNodes.Select(static node => node.Id).Should().NotContain([SampleScene.A, SampleScene.A1, SampleScene.A11, SampleScene.A2]);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.A)).Should().BeNull();
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.AF).Should().BeNull();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodesRemoved([SampleScene.A]), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task Undo_RestoresPositionsLayoutAndExpandedState()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        LayoutQuery.Folder(fixture.Layout, SampleScene.AF)!.IsExpanded = false;
        var rootBefore = fixture.Scene.RootNodes.Select(static node => node.Id).ToList();
        var layoutBefore = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.AtRoot(0));

        _ = await SceneAssert.RoundTripAsync(fixture, new RemoveNodes([SampleScene.A])).ConfigureAwait(false);

        _ = fixture.Scene.RootNodes.Select(static node => node.Id).Should().Equal(rootBefore);
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.AtRoot(0)).Should().Equal(layoutBefore);
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.UnderNode(SampleScene.A, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.A1), LayoutEntryId.Of(SampleScene.AF));
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.AF)!.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    public async Task NodeAndItsDescendant_RemovesTheSubtreeOnce()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var nodesBefore = fixture.Scene.AllNodes.Count();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveNodes([SampleScene.A, SampleScene.A1])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.AllNodes.Select(static node => node.Id).Should().NotContain([SampleScene.A, SampleScene.A1]);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodesRemoved([SampleScene.A]), new SceneChange.LayoutChanged());

        // Undo restores the descendant once, under its parent, not a second time as a root of its own.
        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Scene.AllNodes.Should().HaveCount(nodesBefore);
        _ = fixture.Node(SampleScene.A1).Hierarchy.Parent!.Id.Should().Be(SampleScene.A);
    }

    [TestMethod]
    public async Task TwoRoots_UndoRestoresSiblingOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var rootBefore = fixture.Scene.RootNodes.Select(static node => node.Id).ToList();
        var layoutBefore = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.AtRoot(0));

        _ = await SceneAssert.RoundTripAsync(fixture, new RemoveNodes([SampleScene.B, SampleScene.Cam])).ConfigureAwait(false);

        _ = fixture.Scene.RootNodes.Select(static node => node.Id).Should().Equal(rootBefore);
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.AtRoot(0)).Should().Equal(layoutBefore);
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.F, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G));
    }

    [TestMethod]
    public async Task LockedDescendant_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.A11);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveNodes([SampleScene.A])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.A11);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NoRoots_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveNodes([])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NothingToEdit);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodesAndFolders_AsBatch_AreOneEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var batch = new SceneEditBatch("Delete", [new RemoveNodes([SampleScene.Cam]), new RemoveFolder(SampleScene.F, PromoteChildren: true)]);

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle();
        _ = fixture.Scene.AllNodes.Select(static node => node.Id).Should().NotContain(SampleScene.Cam);
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.F).Should().BeNull();

        // The single entry undoes both.
        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
    }
}

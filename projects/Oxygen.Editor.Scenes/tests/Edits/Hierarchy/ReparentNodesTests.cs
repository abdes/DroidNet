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
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>
/// What is specific to <see cref="ReparentNodes"/> (scene-edits.md 4). Targets, locks, undo and the change stream of
/// every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class ReparentNodesTests
{
    public static IEnumerable<TestDataRow<NodeId>> CycleParents =>
    [
        new(SampleScene.A) { DisplayName = "AUnderA" },
        new(SampleScene.A11) { DisplayName = "AUnderA11" },
    ];

    public static IEnumerable<TestDataRow<LockedParticipantCase>> LockedParticipants =>
    [
        new(new LockedParticipantCase([SampleScene.Cam], SampleScene.B, After: null, Locked: SampleScene.Cam)) { DisplayName = "Node" },
        new(new LockedParticipantCase([SampleScene.A], SampleScene.B, After: null, Locked: SampleScene.A11)) { DisplayName = "Descendant" },
        new(new LockedParticipantCase([SampleScene.Cam], SampleScene.B, After: null, Locked: SampleScene.B)) { DisplayName = "Parent" },
        new(new LockedParticipantCase([SampleScene.Cam], Parent: null, After: SampleScene.Ortho, Locked: SampleScene.Ortho)) { DisplayName = "AfterSibling" },
    ];

    private static NodeId Container { get; } = NodeId.FromValue(SampleScene.Tagged(1, "D1"));

    private static NodeId Moved { get; } = NodeId.FromValue(SampleScene.Tagged(1, "D2"));

    [TestMethod]
    public async Task KeepLocal_MovesUnderTheParent()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        NodePose.Pose(fixture.Node(SampleScene.B));
        var localBefore = NodePose.LocalOf(fixture.Node(SampleScene.B));
        fixture.Changes.Clear();

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B], SampleScene.A, preserveWorld: false)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.B).Parent!.Id.Should().Be(SampleScene.A);
        _ = NodePose.LocalOf(fixture.Node(SampleScene.B)).Should().Be(localBefore);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodesPlaced([SampleScene.B]), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task PreserveWorld_UnderRotatedScaledParent_KeepsTheWorldTransform()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        NodePose.Pose(fixture.Node(SampleScene.B));
        var worldBefore = NodePose.WorldOf(fixture.Node(SampleScene.B));
        var localBefore = NodePose.LocalOf(fixture.Node(SampleScene.B));

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B], SampleScene.A1, preserveWorld: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        NodePose.AssertSameWorld(NodePose.WorldOf(fixture.Node(SampleScene.B)), worldBefore);

        // The world is kept by changing the local transform: a defect that ignores the flag leaves it as it was.
        _ = NodePose.LocalOf(fixture.Node(SampleScene.B)).Should().NotBe(localBefore);
    }

    [TestMethod]
    public async Task PreserveWorld_IgnoringParentTransform_KeepsLocalAndWorld()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        NodePose.Pose(fixture.Node(SampleScene.B));
        fixture.Node(SampleScene.B).IgnoreParentTransform = true;
        var worldBefore = NodePose.WorldOf(fixture.Node(SampleScene.B));
        var localBefore = NodePose.LocalOf(fixture.Node(SampleScene.B));

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B], SampleScene.A1, preserveWorld: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = NodePose.LocalOf(fixture.Node(SampleScene.B)).Should().Be(localBefore);
        NodePose.AssertSameWorld(NodePose.WorldOf(fixture.Node(SampleScene.B)), worldBefore);
    }

    [TestMethod]
    [DataRow(true, DisplayName = "SingularParent")]
    [DataRow(false, DisplayName = "ShearedResult")]
    public async Task PreserveWorld_Unrepresentable_IsInvalid(bool singularParent)
    {
        // A parent that flattens an axis cannot be inverted; a parent that stretches one axis turns a rotated child's
        // world into a shear.
        var parentScale = singularParent ? new Vector3(0, 1, 1) : new Vector3(1, 2, 1);
        var scene = SampleScene.Builder()
            .Node(Container, "Container", transform: SampleScene.Transform(scale: parentScale))
            .Node(Moved, "Moved", transform: SampleScene.Transform(rotation: Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI / 4)))
            .Build();
        using var fixture = await AuthoringFixture.CreateAsync(scene).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([Moved], Container, preserveWorld: true)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.TransformUnrepresentable, Moved);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(CycleParents))]
    public async Task IntoItselfOrDescendant_IsInvalid(NodeId parent)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        // B is a valid participant of the same edit: the edit is refused as a whole, so B stays where it is.
        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B, SampleScene.A], parent, preserveWorld: false)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.HierarchyCycle, SampleScene.A);
        SceneAssert.Unchanged(fixture, before);
        _ = fixture.Node(SampleScene.B).Parent.Should().BeNull();
    }

    [TestMethod]
    public async Task MissingParent_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B], missing, preserveWorld: false)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task MissingAfterSibling_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B], SampleScene.A, preserveWorld: false, after: missing)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(LockedParticipants))]
    public async Task LockedParticipant_IsRefused(LockedParticipantCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(row.Locked);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes(row.Moved, row.Parent, preserveWorld: false, row.After)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, row.Locked);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task AfterSibling_PlacesNodesConsecutively()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var siblings = fixture.Node(SampleScene.A).Children.Select(static node => node.Id).ToList();
        var anchor = siblings.IndexOf(SampleScene.A1);
        List<NodeId> expected = [.. siblings.Take(anchor + 1), SampleScene.B, SampleScene.Cam, .. siblings.Skip(anchor + 1)];

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B, SampleScene.Cam], SampleScene.A, preserveWorld: false, after: SampleScene.A1)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.A).Children.Select(static node => node.Id).Should().Equal(expected);
    }

    [TestMethod]
    public async Task ToRoot_AddsToTheRootNodes()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.A2], parent: null, preserveWorld: false)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.A2).Parent.Should().BeNull();
        _ = fixture.Scene.RootNodes.Select(static node => node.Id).Should().Contain(SampleScene.A2);
        _ = fixture.Node(SampleScene.A).Children.Select(static node => node.Id).Should().NotContain(SampleScene.A2);
    }

    [TestMethod]
    public async Task MovedNode_KeepsItsLayoutEntryFoldersAndExpandedState()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        LayoutQuery.Folder(fixture.Layout, SampleScene.AF)!.IsExpanded = false;

        var result = await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.A], SampleScene.B, preserveWorld: false)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.TryScopeOf(fixture.Layout, LayoutEntryId.Of(SampleScene.A), out var scope).Should().BeTrue();
        _ = scope.Should().Be(SampleScene.B);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.AF))!.Node.Should().Be(SampleScene.A);
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.AF, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.A2));
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.AF)!.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    public async Task Undo_RestoresParentIndexLocalTransformAndLayout()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        NodePose.Pose(fixture.Node(SampleScene.B));
        var rootBefore = fixture.Scene.RootNodes.Select(static node => node.Id).ToList();
        var childrenBefore = fixture.Node(SampleScene.A).Children.Select(static node => node.Id).ToList();
        var localBefore = NodePose.LocalOf(fixture.Node(SampleScene.B));

        _ = await SceneAssert.RoundTripAsync(fixture, new ReparentNodes([SampleScene.B, SampleScene.A2], SampleScene.Cam, preserveWorld: true)).ConfigureAwait(false);

        _ = fixture.Scene.RootNodes.Select(static node => node.Id).Should().Equal(rootBefore);
        _ = fixture.Node(SampleScene.A).Children.Select(static node => node.Id).Should().Equal(childrenBefore);
        _ = NodePose.LocalOf(fixture.Node(SampleScene.B)).Should().Be(localBefore);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.A2)).Should().Be(LayoutPosition.InFolder(SampleScene.AF, 0));
    }
}

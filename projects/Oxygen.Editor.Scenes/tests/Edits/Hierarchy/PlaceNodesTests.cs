// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>
/// What is specific to <see cref="PlaceNodes"/> (scene-edits.md 4): putting nodes back at states captured earlier.
/// Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class PlaceNodesTests
{
    [TestMethod]
    public async Task States_PutNodesBack()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        NodePose.Pose(fixture.Node(SampleScene.B));
        var original = fixture.Snapshot();
        NodePlacementState[] states = [StateOf(fixture, SampleScene.B), StateOf(fixture, SampleScene.Cam)];
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B, SampleScene.Cam], SampleScene.A, preserveWorld: true)).ConfigureAwait(false));
        fixture.Changes.Clear();

        var result = await fixture.Authoring.ExecuteAsync(new PlaceNodes(states)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Snapshot().Document.Should().Be(original.Document);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodesPlaced([SampleScene.B, SampleScene.Cam]), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task MissingNode_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        var state = new NodePlacementState(missing, new NodePosition(Parent: null, 0), new TransformData { Name = "Transform" }, LayoutPosition.AtRoot(0));
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new PlaceNodes([state])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task UndoOfReparent_NodeLockedSince_StillApplies()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new ReparentNodes([SampleScene.B], SampleScene.A, preserveWorld: false)).ConfigureAwait(false));
        fixture.Interaction!.Lock(SampleScene.B);

        var result = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.B).Parent.Should().BeNull();
    }

    private static NodePlacementState StateOf(AuthoringFixture fixture, NodeId id)
    {
        var node = fixture.Node(id);
        var siblings = node.Parent?.Children ?? fixture.Scene.RootNodes;
        return new NodePlacementState(
            id,
            new NodePosition(node.Parent?.Id, siblings.IndexOf(node)),
            NodePose.LocalOf(node),
            LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(id))!);
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// <see cref="SceneNodeCopies"/> (scene-edits.md 4): copies of node subtrees as data, for duplicates and the clipboard.
/// </summary>
[TestClass]
public sealed class SceneNodeCopiesTests
{
    [TestMethod]
    public async Task Copy_GivesNewNodeAndComponentIdsToTheWholeSubtree()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var source = fixture.Node(SampleScene.A).Dehydrate();

        var copies = SceneNodeCopies.Copy(fixture.Scene, [source]);

        var copy = copies.Roots.Should().ContainSingle().Which;
        var copiedNodes = NodeIdsOf(copy);
        var copiedComponents = ComponentIdsOf(copy);
        _ = copiedNodes.Should().HaveSameCount(NodeIdsOf(source)).And.OnlyHaveUniqueItems();
        _ = copiedNodes.Should().NotIntersectWith(fixture.Scene.AllNodes.Select(static node => node.Id));
        _ = copiedComponents.Should().HaveSameCount(ComponentIdsOf(source)).And.OnlyHaveUniqueItems();
        _ = copiedComponents.Should().NotIntersectWith(fixture.Scene.AllNodes.SelectMany(static node => node.Components).Select(static component => component.Id));

        // The map names every source node, at any depth, and only the copies.
        _ = copies.Ids.Keys.Should().BeEquivalentTo(NodeIdsOf(source));
        _ = copies.Ids.Values.Should().BeEquivalentTo(copiedNodes);
    }

    [TestMethod]
    public async Task Copy_ScriptValueToACopiedNode_FollowsTheCopy()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var copies = SceneNodeCopies.Copy(fixture.Scene, [fixture.Node(SampleScene.Script).Dehydrate(), fixture.Node(SampleScene.B).Dehydrate()]);

        _ = TargetOf(copies.Roots[0]).Should().Be(copies.Ids[SampleScene.B]);
        _ = copies.Ids[SampleScene.B].Should().NotBe(SampleScene.B);
    }

    [TestMethod]
    public async Task Copy_ScriptValueToAnUncopiedNode_IsKept()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var copies = SceneNodeCopies.Copy(fixture.Scene, [fixture.Node(SampleScene.Script).Dehydrate()]);

        _ = TargetOf(copies.Roots.Single()).Should().Be(SampleScene.B);
    }

    [TestMethod]
    public async Task Copy_DirectionalLight_ClearsTheSlot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var copies = SceneNodeCopies.Copy(fixture.Scene, [fixture.Node(SampleScene.Sun).Dehydrate()]);

        _ = copies.Roots.Single().Components.OfType<DirectionalLightData>().Single().AtmosphereSlot.Should().Be(AtmosphereLightSlot.None);
        _ = fixture.Node(SampleScene.Sun).Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
    }

    [TestMethod]
    public async Task Copy_TransformGiven_AppliesToTheCopyOnly()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var given = new TransformData { Name = "Transform", Position = new Vector3(7, 8, 9), Scale = new Vector3(2) };
        var transforms = new Dictionary<NodeId, TransformData> { [SampleScene.B] = given };

        var copies = SceneNodeCopies.Copy(fixture.Scene, [fixture.Node(SampleScene.B).Dehydrate()], transforms);

        var copied = copies.Roots.Single().Components.OfType<TransformData>().Single();
        _ = copied.Position.Should().Be(given.Position);
        _ = copied.Scale.Should().Be(given.Scale);
        _ = NodePose.LocalOf(fixture.Node(SampleScene.B)).Position.Should().Be(Vector3.Zero);
    }

    [TestMethod]
    public async Task Copy_NodeWithItsAncestor_CopiesOnce()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var insert = SceneNodeCopies.DuplicateInPlace(fixture.Scene, fixture.Layout, [SampleScene.A, SampleScene.A1]);

        _ = insert.Should().NotBeNull();
        var copy = insert!.Inserts.Should().ContainSingle().Which.Node;
        _ = copy.Name.Should().Be("A");

        // A1 and A11 are in the copy of A, once: A, A1, A11 and A2.
        _ = NodeIdsOf(copy).Should().HaveCount(fixture.Node(SampleScene.A).Descendants().Count() + 1);
    }

    [TestMethod]
    public async Task Copy_SourceEditedAfterwards_LeavesTheCopy()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var copies = SceneNodeCopies.Copy(fixture.Scene, [fixture.Node(SampleScene.B).Dehydrate()]);

        var result = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = copies.Roots.Single().Name.Should().Be("B");
    }

    [TestMethod]
    public async Task DuplicateInPlace_EachCopyFollowsItsSourceInItsFolder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var insert = SceneNodeCopies.DuplicateInPlace(fixture.Scene, fixture.Layout, [SampleScene.B, SampleScene.A2]);
        var inF = LayoutPosition.InFolder(SampleScene.F, 0);
        var inAF = LayoutPosition.InFolder(SampleScene.AF, 0);
        var entriesInF = LayoutScopes.EntriesOf(fixture.Layout, inF);
        var entriesInAF = LayoutScopes.EntriesOf(fixture.Layout, inAF);

        var result = await fixture.Authoring.ExecuteAsync(insert!).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle();

        // B's copy sits between B and G in F, and right after B among the root nodes.
        var afterInF = LayoutScopes.EntriesOf(fixture.Layout, inF);
        _ = afterInF.Should().HaveCount(entriesInF.Count + 1);
        _ = afterInF.Should().HaveElementAt(0, entriesInF[0]);
        _ = afterInF.Should().HaveElementAt(2, entriesInF[1]);
        var copyOfB = afterInF[1].Node!.Value;
        _ = entriesInF.Select(static entry => entry.Node).Should().NotContain(copyOfB);
        var roots = fixture.Scene.RootNodes.Select(static node => node.Id).ToList();
        _ = roots.Should().HaveElementAt(roots.IndexOf(SampleScene.B) + 1, copyOfB);

        // A2's copy follows A2 in AF and among the children of A.
        var afterInAF = LayoutScopes.EntriesOf(fixture.Layout, inAF);
        _ = afterInAF.Should().HaveCount(entriesInAF.Count + 1);
        _ = afterInAF.Should().HaveElementAt(0, entriesInAF[0]);
        var copyOfA2 = afterInAF[1].Node!.Value;
        var children = fixture.Node(SampleScene.A).Hierarchy.Children.Select(static node => node.Id).ToList();
        _ = children.Should().HaveElementAt(children.IndexOf(SampleScene.A2) + 1, copyOfA2);
    }

    [TestMethod]
    public async Task DuplicateInPlace_MissingNode_ReturnsNull()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var insert = SceneNodeCopies.DuplicateInPlace(fixture.Scene, fixture.Layout, [NodeId.New()]);

        _ = insert.Should().BeNull();
    }

    private static List<NodeId> NodeIdsOf(SceneNodeData node)
        => [node.Id, .. (node.Children ?? []).SelectMany(NodeIdsOf)];

    private static List<ComponentId> ComponentIdsOf(SceneNodeData node)
        => [.. node.Components.Select(static component => component.Id), .. (node.Children ?? []).SelectMany(ComponentIdsOf)];

    // The node the node's first script slot targets, as the slot's "target" value.
    private static NodeId TargetOf(SceneNodeData node)
    {
        var value = node.Components.OfType<ScriptingData>().Single().Slots[0].Overrides!["target"];
        return NodeId.FromValue(Guid.Parse(value.Value.GetString()!, CultureInfo.InvariantCulture));
    }
}

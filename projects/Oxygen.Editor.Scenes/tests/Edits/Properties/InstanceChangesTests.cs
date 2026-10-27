// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Properties;

/// <summary>
/// Changing a node an instance brings in (scene instances 6.3, 8.3): a <see cref="SetOverrides"/> write with a node
/// path sets the change in the instance root's changes, any property a change can set, refused while the node or an
/// ancestor, in the instance or above it, is locked, or while the instance's contents are not read; a gesture previews
/// such changes and records one entry.
/// </summary>
[TestClass]
public sealed class InstanceChangesTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly NodeId Body = NodeId.New();
    private static readonly NodeId Wheel = NodeId.New();
    private static readonly PropertyId PositionX = SceneSchema.Transform.PositionX.Id;
    private static readonly PropertyId Material = SceneSchema.Geometry.Surface.Material.Id;

    private static NodePath WheelPath => NodePath.Of(Wheel);

    [TestMethod]
    public async Task AChange_LandsInTheInstanceRootsChangesAndUndoes()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);

        _ = await SceneAssert.RoundTripAsync(fixture, Change(root, PositionX, 2f)).ConfigureAwait(false);
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        _ = ChangeOf(fixture, root, PositionX).Should().Be(2f);
        _ = fixture.Changes.All.SelectMany(static change => change.Changes).OfType<SceneChange.OverridesChanged>().Last().Should().BeEquivalentTo(new SceneChange.OverridesChanged(root, [PositionX]));
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        _ = InstanceOf(fixture, root).Changes.IsEmpty.Should().BeTrue("undo removes the change, never writing the source's value");
    }

    [TestMethod]
    public async Task APropertyNoChangeSets_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(Change(root, SceneSchema.Geometry.GeometryUri.Id, SampleScene.SphereGeometry)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyUnknown, root, SceneSchema.Geometry.GeometryUri.Id);
    }

    [TestMethod]
    public async Task ANodeThatIsNoInstanceRoot_BringsInNothingToChange()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(Change(SampleScene.A, PositionX, 2f)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.A, PositionX);
    }

    [TestMethod]
    public async Task ALockedAncestorInTheInstance_RefusesTheChange()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);
        fixture.Interaction!.Lock(new NodeReference(root, NodePath.Of(Body)));

        var result = await fixture.Authoring.ExecuteAsync(Change(root, PositionX, 2f)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, root);
        _ = InstanceOf(fixture, root).Changes.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    public async Task ALockedInstanceRootOrSceneAncestor_RefusesTheChange()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);
        fixture.Interaction!.Lock(root);

        var result = await fixture.Authoring.ExecuteAsync(Change(root, PositionX, 2f)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, root);
    }

    [TestMethod]
    public async Task UnreadInstanceContents_RefuseTheChange()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture, publish: false).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(Change(root, PositionX, 2f)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.InstanceUnavailable, root);
    }

    [TestMethod]
    public async Task RemovingTheInstance_IsRefusedWhileOneOfItsNodesIsLocked()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);
        fixture.Interaction!.Lock(new NodeReference(root, WheelPath));

        var result = await fixture.Authoring.ExecuteAsync(new RemoveNodes([root])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, root);
        _ = fixture.Scene.AllNodes.Should().Contain(node => node.Id == root);
    }

    [TestMethod]
    public async Task ASurfaceChange_IsRefusedWhenTheNodeRendersAnotherGeometry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);

        var stale = await fixture.Authoring.ExecuteAsync(SurfaceChange(root, SampleScene.SphereGeometry)).ConfigureAwait(false);
        var current = await fixture.Authoring.ExecuteAsync(SurfaceChange(root, SampleScene.CubeGeometry)).ConfigureAwait(false);

        SceneAssert.Invalid(stale, SceneEditCodes.PartStale, root, Material);
        SceneAssert.Applied(current);
    }

    [TestMethod]
    public async Task AGesture_PreviewsChangesAndRecordsOneEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);
        var wheel = new NodeReference(root, WheelPath);

        var gesture = await fixture.Authoring.BeginNodeGestureAsync([wheel], [PositionX], "Move").ConfigureAwait(false);
        SceneAssert.Applied(await gesture.PreviewAsync(Values(wheel, 1f)).ConfigureAwait(false));
        SceneAssert.Applied(await gesture.PreviewAsync(Values(wheel, 3f)).ConfigureAwait(false));
        SceneAssert.Applied(await gesture.CommitAsync().ConfigureAwait(false));

        _ = ChangeOf(fixture, root, PositionX).Should().Be(3f);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        _ = InstanceOf(fixture, root).Changes.IsEmpty.Should().BeTrue("one entry restores where the gesture began");
    }

    [TestMethod]
    public async Task ACancelledGesture_RestoresTheChanges()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(Change(root, PositionX, 2f)).ConfigureAwait(false));
        var wheel = new NodeReference(root, WheelPath);

        var gesture = await fixture.Authoring.BeginNodeGestureAsync([wheel], [PositionX], "Move").ConfigureAwait(false);
        _ = await gesture.PreviewAsync(Values(wheel, 5f)).ConfigureAwait(false);
        await gesture.CancelAsync().ConfigureAwait(false);

        _ = ChangeOf(fixture, root, PositionX).Should().Be(2f);
    }

    [TestMethod]
    public async Task AGestureOnALockedInstancedNode_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);
        fixture.Interaction!.Lock(new NodeReference(root, NodePath.Of(Body)));

        var begin = () => fixture.Authoring.BeginNodeGestureAsync([new NodeReference(root, WheelPath)], [PositionX], "Move");

        _ = (await begin.Should().ThrowAsync<SceneEditRefusedException>().ConfigureAwait(false))
            .Which.Result!.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(SceneEditCodes.TargetLocked);
    }

    private static SetOverrides Change(NodeId root, PropertyId property, object value)
        => new(new Dictionary<NodeId, IReadOnlyList<OverrideWrite>> { [root] = [new OverrideWrite(property, Part: null, LayoutRevision: null, value) { Path = WheelPath }] }, "Change");

    private static SetOverrides SurfaceChange(NodeId root, AssetPath partOwner)
        => new(
            new Dictionary<NodeId, IReadOnlyList<OverrideWrite>>
            {
                [root] =
                [
                    new OverrideWrite(Material, new PartId(SceneSchema.Geometry.Surface.Kind, SampleScene.CubeSurface), SampleScene.CubeLayoutRevision, SampleScene.RedMaterial)
                    {
                        Path = WheelPath,
                        PartOwner = partOwner,
                    },
                ],
            },
            "Material");

    private static Dictionary<NodeReference, PropertyEdit> Values(NodeReference node, float x)
        => new() { [node] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionX, x) };

    private static SceneInstanceComponent InstanceOf(AuthoringFixture fixture, NodeId root)
        => fixture.Node(root).Components.OfType<SceneInstanceComponent>().Single();

    private static object? ChangeOf(AuthoringFixture fixture, NodeId root, PropertyId property)
        => InstanceOf(fixture, root).Changes.Of(WheelPath).Of(property.ComponentKind).Find(property, part: null)?.Value;

    // Places an instance of the car, whose Body holds a Wheel rendering the cube; its structure is read unless told not.
    private static async Task<NodeId> PlaceCarAsync(AuthoringFixture fixture, bool publish = true)
    {
        if (publish)
        {
            var scene = new StructureScene(Guid.NewGuid(), Car.VirtualPath);
            fixture.Structures.Publish(new SceneStructure(
                Car,
                scene,
                [
                    new StructureNode(NodePath.Of(Body), "Body", Parent: null, scene, Instance: null, Geometry: null),
                    new StructureNode(WheelPath, "Wheel", NodePath.Of(Body), scene, Instance: null, SampleScene.CubeGeometry),
                ],
                []));
        }

        var node = SceneNodeTemplates.Instance(fixture.Scene, Car);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false));
        return node.Id;
    }
}

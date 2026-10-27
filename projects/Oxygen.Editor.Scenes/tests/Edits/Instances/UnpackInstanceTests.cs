// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Instances;

/// <summary>
/// Unpacking an instance and copying its nodes (scene instances 6.2, 6.8, 8.3): the nodes become owned nodes with new
/// IDs and their resolved values, a nested instance stays one with this scene's changes re-rooted onto it or is unpacked
/// too, the starting camera and locks follow the copies, changes of nodes the source lacks are dropped with a warning,
/// and undo restores the instance with its changes.
/// </summary>
[TestClass]
public sealed class UnpackInstanceTests
{
    private const string Watchtower = "/Content/Prefabs/Watchtower.oscene";

    // Where the project keeps the authored scene's source.
    private const string WatchtowerFile = "/Content/Prefabs/Watchtower.oscene.json";
    private static readonly PropertyId PositionX = SceneSchema.Transform.PositionX.Id;
    private static readonly PropertyId Rotation = SceneSchema.Transform.Rotation.Id;
    private static readonly NodePath Body = NodePath.Of(NodeId.New());
    private static readonly NodePath Mirror = NodePath.Of(NodeId.New());
    private static readonly NodeId Tower = NodeId.New();
    private static readonly NodeId Torch = NodeId.New();
    private static readonly NodeId Harbor = NodeId.New();

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task AModelInstance_BecomesOwnedNodesWithItsResolvedValues()
    {
        using var fixture = await CreateAsync(Changes((Body, PositionX, 3f), (Mirror, Rotation, new Vector3(0, 0, 30)))).ConfigureAwait(false);

        SceneAssert.Applied(await UnpackAsync(fixture, SampleScene.Car, InstanceUnpack.One).ConfigureAwait(false));

        var car = fixture.Node(SampleScene.Car);
        _ = car.Components.OfType<SceneInstanceComponent>().Should().BeEmpty();
        var body = car.Hierarchy.Children.Should().ContainSingle().Subject;
        _ = body.Metadata.Name.Should().Be("Body");
        _ = body.Id.Should().NotBe(Body.Nodes[0], "a copy gets a new ID");
        _ = body.Transform.LocalPosition.X.Should().Be(3f, "this scene's change wins over the value read");
        _ = body.Transform.LocalRotation.Should().Be(TransformConverter.EulerDegreesToQuaternion(new Vector3(0, 90, 0)), "the read angles give the rotation");
        _ = body.Components.OfType<GeometryComponent>().Single().Geometry.Should().Be(SampleScene.SphereGeometry, "the copy keeps the same geometry");
        var mirror = body.Hierarchy.Children.Should().ContainSingle().Subject;
        _ = mirror.Transform.LocalPosition.X.Should().Be(1f);
        _ = mirror.Transform.LocalRotation.Should().Be(TransformConverter.EulerDegreesToQuaternion(new Vector3(0, 0, 30)), "the change's angles give the rotation");
    }

    [TestMethod]
    public async Task Unpacking_UndoesToTheInstanceWithItsChanges_AndRedoes()
    {
        using var fixture = await CreateAsync(Changes((Body, PositionX, 3f))).ConfigureAwait(false);
        var build = await Conversion(fixture).UnpackAsync(fixture.Scene, SampleScene.Car, InstanceUnpack.One, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await SceneAssert.RoundTripAsync(fixture, build.Value!).ConfigureAwait(false);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        var instance = fixture.Node(SampleScene.Car).Components.OfType<SceneInstanceComponent>().Single();
        _ = instance.Changes.Should().Be(Changes((Body, PositionX, 3f)));
        _ = fixture.Node(SampleScene.Car).Hierarchy.Children.Should().BeEmpty();
    }

    [TestMethod]
    public async Task TheStartingCameraAndLocks_FollowTheCopies_AndUndoPutsThemBack()
    {
        using var fixture = await CreateAsync(InstanceChanges.None).ConfigureAwait(false);
        var camera = new NodeReference(SampleScene.Car, Mirror);
        fixture.Scene.SetStartingCamera(camera);
        fixture.Interaction!.Lock(new NodeReference(SampleScene.Car, Body));

        SceneAssert.Applied(await UnpackAsync(fixture, SampleScene.Car, InstanceUnpack.One).ConfigureAwait(false));

        var body = fixture.Node(SampleScene.Car).Hierarchy.Children.Single();
        var mirror = body.Hierarchy.Children.Single();
        _ = fixture.Scene.StartingCamera.Should().Be(NodeReference.To(mirror.Id));
        _ = fixture.Interaction.LockedNodes().Should().BeEquivalentTo([NodeReference.To(body.Id)]);

        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        _ = fixture.Scene.StartingCamera.Should().Be(camera);
        _ = fixture.Interaction.LockedNodes().Should().BeEquivalentTo([new NodeReference(SampleScene.Car, Body)]);
    }

    [TestMethod]
    public async Task AChangeOfANodeTheSourceLacks_IsDroppedWithAWarning()
    {
        var lost = NodePath.Of(NodeId.New());
        using var fixture = await CreateAsync(Changes((lost, PositionX, 4f))).ConfigureAwait(false);

        var result = await UnpackAsync(fixture, SampleScene.Car, InstanceUnpack.One).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Code == SceneEditCodes.NodePathUnresolved && diagnostic.Node == SampleScene.Car);
    }

    [TestMethod]
    public async Task AnInstanceWhoseContentsAreNotRead_IsRefused()
    {
        using var fixture = await CreateAsync(InstanceChanges.None).ConfigureAwait(false);
        fixture.Structures.Withdraw(SampleScene.CarModel);

        var result = await UnpackAsync(fixture, SampleScene.Car, InstanceUnpack.One).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.InstanceUnavailable, SampleScene.Car);
        _ = fixture.Node(SampleScene.Car).Components.OfType<SceneInstanceComponent>().Should().ContainSingle();
    }

    [TestMethod]
    public async Task AnInstanceChangedSinceItsContentsWereRead_IsRefused()
    {
        using var fixture = await CreateAsync(InstanceChanges.None).ConfigureAwait(false);
        var build = await Conversion(fixture).UnpackAsync(fixture.Scene, SampleScene.Car, InstanceUnpack.One, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Node(SampleScene.Car).Components.OfType<SceneInstanceComponent>().Single().Changes = Changes((Body, PositionX, 8f));

        var result = await fixture.Authoring.ExecuteAsync(build.Value!).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.InstanceUnavailable, SampleScene.Car);
    }

    [TestMethod]
    public async Task AnAuthoredSceneUnpackedOneLevel_KeepsItsInstances_WithThisScenesChangesReRooted()
    {
        using var fixture = await CreateHarborAsync().ConfigureAwait(false);

        SceneAssert.Applied(await UnpackAsync(fixture, Harbor, InstanceUnpack.One).ConfigureAwait(false));

        var children = fixture.Node(Harbor).Hierarchy.Children;
        _ = children.Select(static node => node.Metadata.Name).Should().Equal("Tower", "Torch");
        _ = children[0].Transform.LocalPosition.X.Should().Be(7f, "this scene's change of a node the source owns becomes its value");
        var torch = children[1].Components.OfType<SceneInstanceComponent>().Should().ContainSingle().Subject;
        _ = torch.Source.Should().Be(SampleScene.CarModel);
        _ = torch.Changes.Of(Body).Of(PositionX.ComponentKind).Find(PositionX, part: null)!.Value.Value.Should().Be(9f, "this scene's change wins over the source's");
        _ = children.Select(static node => node.Id).Should().NotContain([Tower, Torch]);
    }

    [TestMethod]
    public async Task AnAuthoredSceneUnpack_UndoesAndRedoesExactly()
    {
        using var fixture = await CreateHarborAsync().ConfigureAwait(false);
        var build = await Conversion(fixture).UnpackAsync(fixture.Scene, Harbor, InstanceUnpack.Complete, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await SceneAssert.RoundTripAsync(fixture, build.Value!).ConfigureAwait(false);
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        _ = fixture.Node(Harbor).Hierarchy.Children.Select(static node => node.Metadata.Name).Should().Equal("Tower", "Torch");
    }

    [TestMethod]
    public async Task AnAuthoredSceneUnpackedCompletely_LeavesNoInstance()
    {
        using var fixture = await CreateHarborAsync().ConfigureAwait(false);

        SceneAssert.Applied(await UnpackAsync(fixture, Harbor, InstanceUnpack.Complete).ConfigureAwait(false));

        var harbor = fixture.Node(Harbor);
        _ = harbor.Descendants().SelectMany(static node => node.Components.OfType<SceneInstanceComponent>()).Should().BeEmpty();
        var torch = harbor.Hierarchy.Children[1];
        var body = torch.Hierarchy.Children.Should().ContainSingle().Subject;
        _ = body.Transform.LocalPosition.X.Should().Be(9f);
    }

    [TestMethod]
    public async Task CopyingAnInstancedNode_GivesItsSubtreeWithTheResolvedValues_LinkedToNothing()
    {
        using var fixture = await CreateAsync(Changes((Mirror, PositionX, 6f))).ConfigureAwait(false);

        var copied = await Conversion(fixture).CopyAsync(fixture.Scene, [new NodeReference(SampleScene.Car, Body), new NodeReference(SampleScene.Car, Mirror)], StructureOf, this.TestContext.CancellationToken).ConfigureAwait(false);

        var body = copied.Value.Should().ContainSingle("the mirror goes with its parent").Subject;
        _ = body.Name.Should().Be("Body");
        _ = body.Components.OfType<SceneInstanceComponentData>().Should().BeEmpty();
        var mirror = body.Children.Should().ContainSingle().Subject;
        _ = mirror.Required<TransformData>().Position.X.Should().Be(6f);

        SceneStructure? StructureOf(AssetPath source) => fixture.Structures.Find(fixture.Project.Context, source);
    }

    // The sample scene's Car, instancing Car.glb (Body with Mirror), with the given changes.
    private static async Task<AuthoringFixture> CreateAsync(InstanceChanges changes)
    {
        var data = SampleScene.Build();
        var components = data.RootNodes.Single(node => node.Id == SampleScene.Car).Components;
        var car = components.OfType<SceneInstanceComponentData>().Single();
        components[components.IndexOf(car)] = car with { Changes = InstanceChangesJson.Write(changes) };
        var fixture = await AuthoringFixture.CreateAsync(data).ConfigureAwait(false);
        PublishCar(fixture);
        return fixture;
    }

    // Harbor instances Watchtower.oscene, which holds Tower and Torch, an instance of Car.glb whose Body Watchtower moves
    // to 5; Harbor moves Tower to 7 and Torch's Body to 9.
    private static async Task<AuthoringFixture> CreateHarborAsync()
    {
        var harbor = SampleScene.Builder()
            .Node(Harbor, "Harbor", components: [new SceneInstanceComponentData
            {
                Id = ComponentId.New(),
                Name = "Scene Instance",
                Source = AssetPath.Parse(Watchtower),
                Changes = InstanceChangesJson.Write(Changes((NodePath.Of(Tower), PositionX, 7f), (NodePath.Of(Torch, Body.Nodes[0]), PositionX, 9f))),
            }
            ])
            .Build();
        var fixture = await AuthoringFixture.CreateAsync(harbor).ConfigureAwait(false);
        var watchtower = new SceneBuilder()
            .Named("Watchtower")
            .Node(Tower, "Tower")
            .Node(Torch, "Torch", components: [new SceneInstanceComponentData
            {
                Id = ComponentId.New(),
                Name = "Scene Instance",
                Source = SampleScene.CarModel,
                Changes = InstanceChangesJson.Write(Changes((Body, PositionX, 5f))),
            }
            ])
            .Build();
        _ = fixture.Project.WriteFile(WatchtowerFile, JsonSerializer.SerializeToUtf8Bytes(watchtower, SceneFileJsonContext.Default.SceneFileData));
        PublishCar(fixture);
        return fixture;
    }

    private static void PublishCar(AuthoringFixture fixture)
    {
        var scene = new StructureScene(Guid.NewGuid(), SampleScene.CarModel.VirtualPath);
        fixture.Structures.Publish(new SceneStructure(
            SampleScene.CarModel,
            scene,
            [
                new StructureNode(Body, "Body", Parent: null, scene, Instance: null, Geometry: SampleScene.SphereGeometry, HasCamera: false),
                new StructureNode(Mirror, "Mirror", Body, scene, Instance: null, Geometry: null, HasCamera: true),
            ],
            []));
        fixture.Structures.PublishValues(SampleScene.CarModel, Body, new InstancedNodeValues("Body", SampleScene.SphereGeometry, [Value(PositionX, 1f), Value(Rotation, new Vector3(0, 90, 0))]));
        fixture.Structures.PublishValues(SampleScene.CarModel, Mirror, new InstancedNodeValues("Mirror", Geometry: null, [Value(PositionX, 1f), Value(Rotation, Vector3.Zero)]));
    }

    private static InstancedValue Value(PropertyId property, object value) => new(property, Guid.Empty, LayoutRevision: null, value);

    private static InstanceChanges Changes(params (NodePath Path, PropertyId Property, object Value)[] changes)
        => changes.Aggregate(InstanceChanges.None, static (all, change) => all.With(
            change.Path,
            all.Of(change.Path).With(all.Of(change.Path).Of(change.Property.ComponentKind).With(new SourceOverride(change.Property, Part: null, LayoutRevision: null, change.Value)))));

    private static InstanceConversion Conversion(AuthoringFixture fixture) => new(fixture.Project.Context, fixture.Structures);

    private static Task<SceneEditResult> UnpackAsync(AuthoringFixture fixture, NodeId instance, InstanceUnpack depth)
        => Conversion(fixture).ExecuteUnpackAsync(fixture.Authoring, instance, depth);
}

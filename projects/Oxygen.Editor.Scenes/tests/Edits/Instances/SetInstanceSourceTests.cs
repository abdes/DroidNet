// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Instances;

/// <summary>
/// Swapping an instance's source (scene instances 6.5, 8.3): the instance takes the new source and keeps the root's
/// own components; its changes move to the nodes of the new source that have their name paths, same-named siblings
/// in order, or are discarded; a change that finds no node is kept, carried from the old source; a source that would
/// instance the scene is refused, and so is keeping changes in a source whose contents are not read.
/// </summary>
[TestClass]
public sealed class SetInstanceSourceTests
{
    private static readonly AssetPath Van = AssetPath.Parse("/Content/Vehicles/Van.glb");
    private static readonly AssetPath Watchtower = AssetPath.Parse("/Content/Prefabs/Watchtower.oscene");
    private static readonly PropertyId PositionX = SceneSchema.Transform.PositionX.Id;
    private static readonly NodePath CarBody = NodePath.Of(NodeId.New());
    private static readonly NodePath CarMirror = NodePath.Of(NodeId.New());
    private static readonly NodePath CarSecondMirror = NodePath.Of(NodeId.New());
    private static readonly NodePath CarWheel = NodePath.Of(NodeId.New());
    private static readonly NodePath VanBody = NodePath.Of(NodeId.New());
    private static readonly NodePath VanMirror = NodePath.Of(NodeId.New());
    private static readonly NodePath VanSecondMirror = NodePath.Of(NodeId.New());

    [TestMethod]
    public async Task KeepingChangesByName_MovesEachToTheNodeWithItsNamePath()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, CarSecondMirror, 2f).ConfigureAwait(false);
        await ChangeAsync(fixture, CarBody, 3f).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, Van, InstanceSwap.KeepByName)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var instance = InstanceOf(fixture);
        _ = instance.Source.Should().Be(Van);
        _ = instance.Changes.Nodes.Keys.Should().BeEquivalentTo([VanSecondMirror, VanBody], "the second Mirror matches the second Mirror");
        _ = ValueOf(instance, VanSecondMirror).Should().Be(2f);
        _ = instance.Changes.Of(VanSecondMirror).Names.Should().Be("Body/Mirror[2]");
        _ = instance.Changes.Of(VanSecondMirror).CarriedFrom.Should().BeNull("the change found its node");
        _ = fixture.Changes.All[fixture.Changes.All.Count - 1].Changes.Should().ContainEquivalentOf(new SceneChange.InstanceSourceChanged(SampleScene.Car));
    }

    [TestMethod]
    public async Task AChangeWhoseNodeTheNewSourceLacks_IsKeptCarriedFromTheOldSource()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, CarWheel, 4f).ConfigureAwait(false);

        SceneAssert.Warned(await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, Van, InstanceSwap.KeepByName)).ConfigureAwait(false), SceneEditCodes.NodePathUnresolved, SampleScene.Car);

        var kept = InstanceOf(fixture).Changes.Of(CarWheel);
        _ = ValueOf(InstanceOf(fixture), CarWheel).Should().Be(4f, "the change stays on its path");
        _ = kept.CarriedFrom.Should().Be(SampleScene.CarModel);
        _ = kept.Names.Should().Be("Body/Wheel", "the kept change names the node it was made on");
    }

    [TestMethod]
    public async Task KeptCarriedChanges_SaveAndReadBack()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, CarWheel, 4f).ConfigureAwait(false);
        SceneAssert.Warned(await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, Van, InstanceSwap.KeepByName)).ConfigureAwait(false), SceneEditCodes.NodePathUnresolved, SampleScene.Car);
        var instance = InstanceOf(fixture);

        var json = JsonSerializer.Serialize((SceneInstanceComponentData)instance.Dehydrate(), SceneJsonContext.Default.SceneInstanceComponentData);
        var read = JsonSerializer.Deserialize(json, SceneJsonContext.Default.SceneInstanceComponentData)!;

        _ = read.Source.Should().Be(Van);
        _ = InstanceChangesJson.Read(read.Changes).Should().Be(instance.Changes);
    }

    [TestMethod]
    public async Task KeepingChangesByName_UndoesAndRedoesExactly()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, CarSecondMirror, 2f).ConfigureAwait(false);
        await ChangeAsync(fixture, CarWheel, 4f).ConfigureAwait(false);

        _ = await SceneAssert.RoundTripAsync(fixture, new SetInstanceSource(SampleScene.Car, Van, InstanceSwap.KeepByName)).ConfigureAwait(false);
    }

    [TestMethod]
    public async Task DiscardingChanges_RemovesEveryChange_AndUndoRestoresThem()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, CarBody, 3f).ConfigureAwait(false);
        var before = InstanceOf(fixture).Changes;

        _ = await SceneAssert.RoundTripAsync(fixture, new SetInstanceSource(SampleScene.Car, Van, InstanceSwap.Discard)).ConfigureAwait(false);
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        _ = InstanceOf(fixture).Changes.IsEmpty.Should().BeTrue();
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        _ = InstanceOf(fixture).Changes.Should().Be(before);
        _ = InstanceOf(fixture).Source.Should().Be(SampleScene.CarModel);
    }

    [TestMethod]
    public async Task ASourceThatInstancesTheScene_IsRefused()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        fixture.References.Set(Watchtower, AssetPath.Parse($"/Content/{fixture.Scene.Name}.oscene"));

        var result = await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, Watchtower, InstanceSwap.KeepByName)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.InstanceCycle, SampleScene.Car);
        _ = InstanceOf(fixture).Source.Should().Be(SampleScene.CarModel);
    }

    [TestMethod]
    public async Task KeepingChangesInASourceNotRead_IsRefused_ButDiscardingThemSwaps()
    {
        using var fixture = await CreateAsync(publishVan: false).ConfigureAwait(false);
        await ChangeAsync(fixture, CarBody, 3f).ConfigureAwait(false);

        var kept = await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, Van, InstanceSwap.KeepByName)).ConfigureAwait(false);
        var discarded = await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, Van, InstanceSwap.Discard)).ConfigureAwait(false);

        SceneAssert.Invalid(kept, SceneEditCodes.InstanceUnavailable, SampleScene.Car);
        SceneAssert.Applied(discarded);
        _ = InstanceOf(fixture).Source.Should().Be(Van);
    }

    [TestMethod]
    public async Task ANodeThatIsNoInstanceRoot_HasNoSourceToSwap()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.A, Van, InstanceSwap.KeepByName)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.A);
    }

    [TestMethod]
    public async Task ASourceThatIsNeitherAModelNorAScene_IsRefused()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, SampleScene.RedMaterial, InstanceSwap.KeepByName)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid, SampleScene.Car);
    }

    // The sample scene's Car instance, with the structures of Car.glb (Body with Mirror, Mirror and Wheel) and, unless
    // told not, Van.glb (Body with Mirror and Mirror).
    private static async Task<AuthoringFixture> CreateAsync(bool publishVan = true)
    {
        var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Structures.Publish(Structure(SampleScene.CarModel, (CarBody, "Body", null), (CarMirror, "Mirror", CarBody), (CarSecondMirror, "Mirror", CarBody), (CarWheel, "Wheel", CarBody)));
        if (publishVan)
        {
            fixture.Structures.Publish(Structure(Van, (VanBody, "Body", null), (VanMirror, "Mirror", VanBody), (VanSecondMirror, "Mirror", VanBody)));
        }

        return fixture;
    }

    private static SceneStructure Structure(AssetPath source, params (NodePath Path, string Name, NodePath? Parent)[] nodes)
    {
        var scene = new StructureScene(Guid.NewGuid(), source.VirtualPath);
        return new SceneStructure(
            source,
            scene,
            [.. nodes.Select(node => new StructureNode(node.Path, node.Name, node.Parent, scene, Instance: null, Geometry: null, HasCamera: false))],
            []);
    }

    private static async Task ChangeAsync(AuthoringFixture fixture, NodePath path, float x)
        => SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new SetOverrides(
            new Dictionary<NodeId, IReadOnlyList<OverrideWrite>> { [SampleScene.Car] = [new OverrideWrite(PositionX, Part: null, LayoutRevision: null, x) { Path = path }] },
            "Change")).ConfigureAwait(false));

    private static SceneInstanceComponent InstanceOf(AuthoringFixture fixture)
        => fixture.Node(SampleScene.Car).Components.OfType<SceneInstanceComponent>().Single();

    private static object? ValueOf(SceneInstanceComponent instance, NodePath path)
        => instance.Changes.Of(path).Of(PositionX.ComponentKind).Find(PositionX, part: null)?.Value;
}

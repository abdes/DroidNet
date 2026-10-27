// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Models;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Instances;

/// <summary>
/// Kept changes (scene instances 6.4, 8.3, 8.10): an instance's changes whose node its source no longer holds are
/// listed, removed, and reassigned where they were lost: in the model's import settings, which reimport, for a change
/// made on the model; in the instance, for any other. Each is one undoable edit.
/// </summary>
[TestClass]
public sealed class KeptChangeTests
{
    private const string LostAnchor = "Body/Door_FL";
    private const string RenamedAnchor = "Body/Door_Front_L";
    private const string MirrorAnchor = "Body/Mirror";

    private static readonly PropertyId PositionX = SceneSchema.Transform.PositionX.Id;
    private static readonly PropertyId PositionY = SceneSchema.Transform.PositionY.Id;
    private static readonly NodePath Body = NodePath.Of(NodeId.New());
    private static readonly NodePath LostDoor = NodePath.Of(NodeId.New());
    private static readonly NodePath RenamedDoor = NodePath.Of(NodeId.New());
    private static readonly NodePath Mirror = NodePath.Of(NodeId.New());
    private static readonly NodePath Gate = NodePath.Of(NodeId.New());

    [TestMethod]
    public async Task KeptChanges_AreTheChangesWhoseNodeTheSourceLacks()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, Body, PositionX, 1f).ConfigureAwait(false);

        var kept = KeptChanges.Of(CarOf(fixture), fixture.Structures.Find(fixture.Project.Context, SampleScene.CarModel));

        _ = kept.Select(static change => change.Path).Should().Equal([LostDoor], "Body resolves; the door is gone");
        _ = kept[0].Names.Should().Be(LostAnchor);
        _ = KeptChanges.Of(CarOf(fixture), structure: null).Should().BeEmpty("a change is kept only against a read source");
    }

    [TestMethod]
    public async Task AChangeOfAnInstancedNode_RecordsTheNodesNames()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);

        await ChangeAsync(fixture, Mirror, PositionX, 1f).ConfigureAwait(false);

        _ = CarOf(fixture).Changes.Of(Mirror).Names.Should().Be("Body/Mirror", "a kept change shows the names its node last had");
    }

    [TestMethod]
    public async Task RemovingKeptChanges_RemovesThem_AndUndoRestoresThem()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        var before = CarOf(fixture).Changes;

        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new RemoveKeptChanges(SampleScene.Car, [LostDoor])).ConfigureAwait(false));
        _ = CarOf(fixture).Changes.Nodes.Should().NotContainKey(LostDoor);

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = CarOf(fixture).Changes.Should().Be(before);
    }

    [TestMethod]
    public async Task RemovingAChangeWhoseNodeResolves_IsRefused()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, Body, PositionX, 1f).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveKeptChanges(SampleScene.Car, [Body])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.InstanceUnavailable, SampleScene.Car);
    }

    [TestMethod]
    public async Task KeptChangesOfASourceNotRead_AreNotToldApart()
    {
        using var fixture = await CreateAsync(publishCar: false).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveKeptChanges(SampleScene.Car, [LostDoor])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.InstanceUnavailable, SampleScene.Car);
    }

    [TestMethod]
    public async Task AChangeMadeOnTheModel_IsRepairedInTheModel()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);

        _ = KeptChanges.RepairOf(CarOf(fixture), LostDoor, Record(fixture)).Should().Be(KeptChangeRepair.Model);
        _ = KeptChanges.RepairOf(CarOf(fixture), LostDoor, record: null).Should().Be(KeptChangeRepair.Instance, "without the model's record, no import reports the node as vanished");
    }

    [TestMethod]
    public async Task ReassigningInTheModel_ContinuesTheLostAnchor_AndReimports()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ReassignKeptChange(SampleScene.Car, LostDoor, RenamedDoor)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = Record(fixture).NodeContinuity.Should().Equal([new ModelNodeContinuity(RenamedAnchor, LostAnchor)]);
        _ = fixture.ModelRecords.Reimported.Should().Equal([SampleScene.CarModel]);
        var change = CarOf(fixture).Changes.Of(LostDoor);
        _ = ValueOf(change, PositionX).Should().Be(5f, "the change stays on the lost node's ID, which the chosen node takes once reimported");
        _ = change.Names.Should().Be(RenamedAnchor);
    }

    [TestMethod]
    public async Task ReassigningInTheModel_ToANodeWithChanges_WarnsTheyBecomeKept()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        await ChangeAsync(fixture, Mirror, PositionY, 2f).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ReassignKeptChange(SampleScene.Car, LostDoor, Mirror)).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.NodePathUnresolved, SampleScene.Car);
    }

    [TestMethod]
    public async Task UndoingARepairInTheModel_RestoresTheRecordAndReimports()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        var before = fixture.ModelRecords.Read(SampleScene.CarModel)!;
        var changes = CarOf(fixture).Changes;
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new ReassignKeptChange(SampleScene.Car, LostDoor, RenamedDoor)).ConfigureAwait(false));

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));

        _ = fixture.ModelRecords.Read(SampleScene.CarModel).Should().Equal(before);
        _ = fixture.ModelRecords.Reimported.Should().Equal([SampleScene.CarModel, SampleScene.CarModel]);
        _ = CarOf(fixture).Changes.Should().Be(changes);
    }

    [TestMethod]
    public async Task UndoingARepairInTheModel_OnceTheRecordChanged_IsRefusedWithSourceChanged()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new ReassignKeptChange(SampleScene.Car, LostDoor, RenamedDoor)).ConfigureAwait(false));
        var edited = Record(fixture).WithContinuity(MirrorAnchor, "Body/Old_Mirror").ToBytes();
        fixture.ModelRecords.Set(SampleScene.CarModel, edited);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Refused(undone, SceneEditCodes.SourceChanged, SampleScene.Car);
        _ = fixture.ModelRecords.Read(SampleScene.CarModel).Should().Equal(edited, "the record keeps its newer settings");
    }

    [TestMethod]
    public async Task ReassigningInTheModel_ANodeThatContinuesAnother_IsRefused()
    {
        using var fixture = await CreateAsync(record => record.WithContinuity(RenamedAnchor, "Body/Old_Door")).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ReassignKeptChange(SampleScene.Car, LostDoor, RenamedDoor)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.OverrideInvalid, SampleScene.Car);
        _ = fixture.ModelRecords.Reimported.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ReassigningToANodeTheSourceLacks_IsRefused()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ReassignKeptChange(SampleScene.Car, LostDoor, Gate)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.NodePathUnresolved, SampleScene.Car);
    }

    [TestMethod]
    public async Task ReassigningAChangeOfASceneInstance_MovesItInTheInstance_ItsValuesWinning()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        var tower = new StructureScene(Guid.NewGuid(), SampleScene.TowerScene.VirtualPath);
        fixture.Structures.Publish(new SceneStructure(SampleScene.TowerScene, tower, [new StructureNode(Gate, "Gate", Parent: null, tower, Instance: null, Geometry: null, HasCamera: false)], []));
        await ChangeAsync(fixture, SampleScene.Tower, Gate, PositionX, 7f).ConfigureAwait(false);
        await ChangeAsync(fixture, SampleScene.Tower, Gate, PositionY, 3f).ConfigureAwait(false);
        var before = InstanceOf(fixture, SampleScene.Tower).Changes;

        var result = await fixture.Authoring.ExecuteAsync(new ReassignKeptChange(SampleScene.Tower, SampleScene.Post, Gate)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var changes = InstanceOf(fixture, SampleScene.Tower).Changes;
        _ = changes.Nodes.Keys.Should().Equal([Gate]);
        _ = ValueOf(changes.Of(Gate), PositionX).Should().Be(2f, "the reassigned change's values win");
        _ = ValueOf(changes.Of(Gate), PositionY).Should().Be(3f, "the node's own other changes stay");
        _ = changes.Of(Gate).Names.Should().Be("Gate");
        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.Should().Be(before);
    }

    [TestMethod]
    public async Task Targets_ListTheNodesNewInTheLastImportFirst_WithWhatChoosingEachCosts()
    {
        using var fixture = await CreateAsync(record => record.WithContinuity(MirrorAnchor, "Body/Old_Mirror")).ConfigureAwait(false);
        await ChangeAsync(fixture, Body, PositionY, 2f).ConfigureAwait(false);
        var car = CarOf(fixture);
        var change = KeptChanges.Of(car, fixture.Structures.Find(fixture.Project.Context, SampleScene.CarModel)).Single();

        var targets = KeptChanges.TargetsOf(fixture.Scene, car, change, fixture.Structures.Find(fixture.Project.Context, SampleScene.CarModel)!, Record(fixture));

        _ = targets.Select(static target => target.Path).Should().Equal([RenamedDoor, Body, Mirror], "the node new in the last import comes first");
        _ = targets[0].IsNew.Should().BeTrue();
        _ = targets[1].Kept.Should().Be(1, "Body's own change becomes kept if Body continues the door");
        _ = targets[2].Refusal.Should().NotBeNull("Mirror already continues another node");
    }

    [TestMethod]
    public async Task SwappingWithChangesKeptByName_WarnsOfTheChangesItKeeps()
    {
        using var fixture = await CreateAsync().ConfigureAwait(false);
        var van = AssetPath.Parse("/Content/Vehicles/Van.glb");
        var scene = new StructureScene(Guid.NewGuid(), van.VirtualPath);
        fixture.Structures.Publish(new SceneStructure(van, scene, [new StructureNode(NodePath.Of(NodeId.New()), "Body", Parent: null, scene, Instance: null, Geometry: null, HasCamera: false)], []));

        var result = await fixture.Authoring.ExecuteAsync(new SetInstanceSource(SampleScene.Car, van, InstanceSwap.KeepByName)).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.NodePathUnresolved, SampleScene.Car);
    }

    // The sample scene's Car, whose change of Body/Door_FL a reimport lost: Car.glb now holds Body, Body/Door_Front_L (new
    // in that import) and Body/Mirror, and its record reports the door as vanished.
    private static async Task<AuthoringFixture> CreateAsync(Func<ModelImportSettings, ModelImportSettings>? edit = null, bool publishCar = true)
    {
        var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        if (publishCar)
        {
            var scene = new StructureScene(Guid.NewGuid(), SampleScene.CarModel.VirtualPath);
            fixture.Structures.Publish(new SceneStructure(
                SampleScene.CarModel,
                scene,
                [
                    new StructureNode(Body, "Body", Parent: null, scene, Instance: null, Geometry: null, HasCamera: false),
                    new StructureNode(RenamedDoor, "Door_Front_L", Body, scene, Instance: null, Geometry: null, HasCamera: false),
                    new StructureNode(Mirror, "Mirror", Body, scene, Instance: null, Geometry: null, HasCamera: false),
                ],
                []));
        }

        var recordPath = ModelImportSettings.PathFor(fixture.Project.Root.FullPath, Guid.CreateVersion7());
        var record = ModelImportSettings.Create(recordPath, fixture.Project.PathOf("/Content/Vehicles/Car.glb").FullPath, SampleScene.CarModel.VirtualPath, ModelImportSettings.DefaultSettings("Car")) with
        {
            PublishedNodes = [new("Body", Body.Nodes[0].Value), new(RenamedAnchor, RenamedDoor.Nodes[0].Value), new(MirrorAnchor, Mirror.Nodes[0].Value)],
            NewNodes = [new(RenamedAnchor, RenamedDoor.Nodes[0].Value)],
            VanishedNodes = [new(LostAnchor, LostDoor.Nodes[0].Value)],
        };
        fixture.ModelRecords.Set(SampleScene.CarModel, (edit?.Invoke(record) ?? record).ToBytes());

        // The change was made while the door was there, and names it; no edit can make it now that the door is gone.
        var car = CarOf(fixture);
        car.Changes = car.Changes.With(
            LostDoor,
            InstancedNodeChanges.None.With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(new SourceOverride(PositionX, Part: null, LayoutRevision: null, 5f))).WithNames(LostAnchor));
        return fixture;
    }

    private static Task ChangeAsync(AuthoringFixture fixture, NodePath path, PropertyId property, float value)
        => ChangeAsync(fixture, SampleScene.Car, path, property, value);

    private static async Task ChangeAsync(AuthoringFixture fixture, NodeId root, NodePath path, PropertyId property, float value)
        => SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new SetOverrides(
            new Dictionary<NodeId, IReadOnlyList<OverrideWrite>> { [root] = [new OverrideWrite(property, Part: null, LayoutRevision: null, value) { Path = path }] },
            "Change")).ConfigureAwait(false));

    private static ModelImportSettings Record(AuthoringFixture fixture) => ModelImportSettings.Parse(fixture.ModelRecords.Read(SampleScene.CarModel)!);

    private static SceneInstanceComponent CarOf(AuthoringFixture fixture) => InstanceOf(fixture, SampleScene.Car);

    private static SceneInstanceComponent InstanceOf(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<SceneInstanceComponent>().Single();

    private static object? ValueOf(InstancedNodeChanges changes, PropertyId property)
        => changes.Of(property.ComponentKind).Find(property, part: null)?.Value;
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Instances;

/// <summary>
/// Applying an instance's changes to an authored scene on their node paths (scene instances 6.7, 8.3): the scene's file
/// takes them, as stored values of its nodes or changes of its own instances, they leave the instance, the file's
/// previous bytes come back on undo, and undo is refused once the file changed since. A model is not an authored scene.
/// </summary>
[TestClass]
public sealed class ApplyToSourceTests
{
    private const string Watchtower = "/Content/Prefabs/Watchtower.oscene";
    private const string WatchtowerFile = "/Content/Prefabs/Watchtower.oscene.json";
    private const string TorchScene = "/Content/Prefabs/Torch.oscene";
    private const string TorchFile = "/Content/Prefabs/Torch.oscene.json";
    private static readonly PropertyId PositionX = SceneSchema.Transform.PositionX.Id;
    private static readonly PropertyId PositionY = SceneSchema.Transform.PositionY.Id;
    private static readonly NodeId Harbor = NodeId.New();
    private static readonly NodeId Torch = NodeId.New();
    private static readonly NodeId Flame = NodeId.New();

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task AChangeOfANodeTheSourceOwns_BecomesItsStoredValue_AndLeavesTheInstance()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        SceneAssert.Applied(await this.ApplyAsync(fixture, SampleScene.Tower, ChangeSelection.Instance, new ApplyTarget(SampleScene.TowerScene, 0)).ConfigureAwait(false));

        _ = (await this.PostXAsync(fixture).ConfigureAwait(false)).Should().Be(2f);
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.IsEmpty.Should().BeTrue("the change left the instance");
        _ = fixture.Authoring.Scene.AllNodes.Should().HaveCount(SampleScene.AllNodes.Count, "the scene's own nodes are untouched");
    }

    [TestMethod]
    public async Task Undo_RestoresTheSourceFileAndTheChanges_AndRedoAppliesAgain()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = SampleScene.TowerSource();
        _ = await this.ApplyAsync(fixture, SampleScene.Tower, ChangeSelection.Instance, new ApplyTarget(SampleScene.TowerScene, 0)).ConfigureAwait(false);

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));

        _ = (await this.FileAsync(fixture, SampleScene.TowerFile).ConfigureAwait(false)).Should().Equal(before, "the file's previous bytes come back");
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.Should().Be(SampleScene.TowerChanges);

        SceneAssert.Applied(await fixture.Authoring.RedoAsync().ConfigureAwait(false));

        _ = (await this.PostXAsync(fixture).ConfigureAwait(false)).Should().Be(2f);
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    public async Task Undo_OnceTheSourceFileChanged_IsRefusedWithSourceChanged_AndChangesNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await this.ApplyAsync(fixture, SampleScene.Tower, ChangeSelection.Instance, new ApplyTarget(SampleScene.TowerScene, 0)).ConfigureAwait(false);
        var edited = SampleScene.TowerSource(postX: 7f);
        _ = fixture.Project.WriteFile(SampleScene.TowerFile, edited);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Refused(undone, SceneEditCodes.SourceChanged, SampleScene.Tower);
        _ = undone.Diagnostics.Single().Message.Should().Contain("Tower.oscene changed since the changes were applied to it");
        _ = (await this.FileAsync(fixture, SampleScene.TowerFile).ConfigureAwait(false)).Should().Equal(edited, "the source keeps its newer contents");
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.IsEmpty.Should().BeTrue();
        _ = fixture.History.CanUndo.Should().BeTrue("the refused entry stays in the history");
    }

    [TestMethod]
    public async Task AnApply_BuiltBeforeTheSourceFileChanged_IsRefusedWithSourceChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var built = await Application(fixture).BuildAsync(fixture.Scene, SampleScene.Tower, ChangeSelection.Instance, new ApplyTarget(SampleScene.TowerScene, 0), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Project.WriteFile(SampleScene.TowerFile, SampleScene.TowerSource(postX: 7f));

        var result = await fixture.Authoring.ExecuteAsync(built.Value!).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.SourceChanged, SampleScene.Tower);
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.Should().Be(SampleScene.TowerChanges);
    }

    [TestMethod]
    public async Task ALockedNodeOrInstance_RefusesTheApply()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var post = new NodeReference(SampleScene.Tower, SampleScene.Post);
        fixture.Interaction!.Lock(post);

        var result = await this.ApplyAsync(fixture, SampleScene.Tower, ChangeSelection.Instance, new ApplyTarget(SampleScene.TowerScene, 0)).ConfigureAwait(false);

        using var rootLocked = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        rootLocked.Interaction!.Lock(SampleScene.Tower);
        var underRoot = await this.ApplyAsync(rootLocked, SampleScene.Tower, ChangeSelection.Instance, new ApplyTarget(SampleScene.TowerScene, 0)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.Tower);
        SceneAssert.Refused(underRoot, SceneEditCodes.TargetLocked, SampleScene.Tower);
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.Should().Be(SampleScene.TowerChanges);
    }

    [TestMethod]
    public async Task AModelSource_IsNotOffered_AndIsRefusedWithTheReason()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var targets = await Application(fixture).TargetsAsync(fixture.Scene, NodeReference.To(SampleScene.Car), this.TestContext.CancellationToken).ConfigureAwait(false);
        var result = await this.ApplyAsync(fixture, SampleScene.Car, ChangeSelection.Instance, new ApplyTarget(SampleScene.CarModel, 0)).ConfigureAwait(false);

        _ = targets.Should().BeEmpty();
        SceneAssert.Refused(result, SceneEditCodes.AssetReferenceInvalid, SampleScene.Car);
        _ = result.Diagnostics.Single().Message.Should().Be("Car.glb is an imported model, not an authored scene: its import makes its nodes and a reimport rewrites them, so changes stay on the instance.");
    }

    [TestMethod]
    public async Task ARow_MovesOnlyItsValues()
    {
        using var fixture = await AuthoringFixture.CreateAsync(TowerWith(Change(Change(InstancedNodeChanges.None, PositionX, 2f), PositionY, 3f))).ConfigureAwait(false);

        SceneAssert.Applied(await this.ApplyAsync(fixture, SampleScene.Tower, ChangeSelection.OfValues(SampleScene.Post, [PositionX]), new ApplyTarget(SampleScene.TowerScene, 0)).ConfigureAwait(false));

        _ = (await this.PostXAsync(fixture).ConfigureAwait(false)).Should().Be(2f);
        var remaining = InstanceOf(fixture, SampleScene.Tower).Changes.Of(SampleScene.Post).Of(PositionY.ComponentKind);
        _ = remaining.All.Select(static change => change.Property).Should().Equal([PositionY], "the other value of the node stays on the instance");
    }

    [TestMethod]
    public async Task AChangeOfANodeTheSourceLacks_StaysOnTheInstance_WithAWarning()
    {
        var lost = NodePath.Of(NodeId.New());
        var changes = SampleScene.TowerChanges.With(lost, Change(InstancedNodeChanges.None, PositionX, 4f).WithNames("Gate"));
        using var fixture = await AuthoringFixture.CreateAsync(TowerWith(changes)).ConfigureAwait(false);

        var result = await this.ApplyAsync(fixture, SampleScene.Tower, ChangeSelection.Instance, new ApplyTarget(SampleScene.TowerScene, 0)).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Code == SceneEditCodes.NodePathUnresolved && diagnostic.Message.Contains("Gate", StringComparison.Ordinal));
        _ = InstanceOf(fixture, SampleScene.Tower).Changes.Nodes.Keys.Should().Equal([lost]);
    }

    [TestMethod]
    public async Task ANodeOfANestedScene_OffersEveryAuthoredSceneOnItsPath()
    {
        using var fixture = await CreateHarborAsync().ConfigureAwait(false);

        var targets = await Application(fixture).TargetsAsync(fixture.Scene, new NodeReference(Harbor, NodePath.Of(Torch, Flame)), this.TestContext.CancellationToken).ConfigureAwait(false);
        var whole = await Application(fixture).TargetsAsync(fixture.Scene, NodeReference.To(Harbor), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = targets.Select(static target => target.Label).Should().Equal("Apply to Watchtower", "Apply to Torch");
        _ = whole.Select(static target => target.Label).Should().Equal("Apply to Watchtower");
    }

    [TestMethod]
    public async Task AppliedToTheOuterScene_ADeeperChange_BecomesAChangeOfItsInstance_WinningOverItsOwn()
    {
        using var fixture = await CreateHarborAsync().ConfigureAwait(false);

        SceneAssert.Applied(await this.ApplyAsync(fixture, Harbor, ChangeSelection.OfNode(NodePath.Of(Torch, Flame)), new ApplyTarget(AssetPath.Parse(Watchtower), 0)).ConfigureAwait(false));

        var watchtower = await SceneFiles.ReadAsync(fixture.Project.Context, AssetPath.Parse(Watchtower), this.TestContext.CancellationToken).ConfigureAwait(false);
        var torch = watchtower!.AllNodes.Single(node => node.Id == Torch).Components.OfType<SceneInstanceComponent>().Single();
        var flame = torch.Changes.Of(NodePath.Of(Flame));
        _ = flame.Of(PositionX.ComponentKind).Find(PositionX, part: null)!.Value.Value.Should().Be(9f, "the moved value wins over Watchtower's own");
        _ = flame.Names.Should().Be("Flame", "the names start below Watchtower's instance root");
        _ = InstanceOf(fixture, Harbor).Changes.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    public async Task AppliedToTheInnerScene_ADeeperChange_BecomesItsStoredValue()
    {
        using var fixture = await CreateHarborAsync().ConfigureAwait(false);

        SceneAssert.Applied(await this.ApplyAsync(fixture, Harbor, ChangeSelection.OfNode(NodePath.Of(Torch, Flame)), new ApplyTarget(AssetPath.Parse(TorchScene), 1)).ConfigureAwait(false));

        var torch = await SceneFiles.ReadAsync(fixture.Project.Context, AssetPath.Parse(TorchScene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = torch!.AllNodes.Single(node => node.Id == Flame).Transform.LocalPosition.X.Should().Be(9f);
        _ = InstanceOf(fixture, Harbor).Changes.IsEmpty.Should().BeTrue();

        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        var restored = await SceneFiles.ReadAsync(fixture.Project.Context, AssetPath.Parse(TorchScene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = restored!.AllNodes.Single(node => node.Id == Flame).Transform.LocalPosition.X.Should().Be(0f);
    }

    [TestMethod]
    public async Task TheWholeInstance_DoesNotApplyToASceneBelowItsSource()
    {
        using var fixture = await CreateHarborAsync().ConfigureAwait(false);

        var result = await this.ApplyAsync(fixture, Harbor, ChangeSelection.Instance, new ApplyTarget(AssetPath.Parse(TorchScene), 1)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.NothingToEdit, Harbor);
    }

    // The sample scene with Tower's changes replaced.
    private static SceneFileData TowerWith(InstanceChanges changes)
    {
        var data = SampleScene.Build();
        var components = data.RootNodes.Single(node => node.Id == SampleScene.Tower).Components;
        var tower = components.OfType<SceneInstanceComponentData>().Single();
        components[components.IndexOf(tower)] = tower with { Changes = InstanceChangesJson.Write(changes) };
        return data;
    }

    private static SceneFileData TowerWith(InstancedNodeChanges post)
        => TowerWith(InstanceChanges.None.With(SampleScene.Post, post.WithNames("Post")));

    private static InstancedNodeChanges Change(InstancedNodeChanges changes, PropertyId property, object value)
        => changes.With(changes.Of(property.ComponentKind).With(new SourceOverride(property, Part: null, LayoutRevision: null, value)));

    // Harbor instances Watchtower.oscene, whose Torch instances Torch.oscene, which owns Flame. Watchtower moves Flame to
    // 5; Harbor moves it to 9.
    private static async Task<AuthoringFixture> CreateHarborAsync()
    {
        var harbor = SampleScene.Builder()
            .Node(Harbor, "Harbor", components: [new SceneInstanceComponentData
            {
                Id = ComponentId.New(),
                Name = "Scene Instance",
                Source = AssetPath.Parse(Watchtower),
                Changes = InstanceChangesJson.Write(InstanceChanges.None.With(NodePath.Of(Torch, Flame), Change(InstancedNodeChanges.None, PositionX, 9f).WithNames("Torch/Flame"))),
            }
            ])
            .Build();
        var fixture = await AuthoringFixture.CreateAsync(harbor).ConfigureAwait(false);
        var watchtower = new SceneBuilder()
            .Named("Watchtower")
            .Node(Torch, "Torch", components: [new SceneInstanceComponentData
            {
                Id = ComponentId.New(),
                Name = "Scene Instance",
                Source = AssetPath.Parse(TorchScene),
                Changes = InstanceChangesJson.Write(InstanceChanges.None.With(NodePath.Of(Flame), Change(InstancedNodeChanges.None, PositionX, 5f).WithNames("Flame"))),
            }
            ])
            .Build();
        var torch = new SceneBuilder().Named("Torch").Node(Flame, "Flame").Build();
        _ = fixture.Project.WriteFile(WatchtowerFile, JsonSerializer.SerializeToUtf8Bytes(watchtower, SceneFileJsonContext.Default.SceneFileData));
        _ = fixture.Project.WriteFile(TorchFile, JsonSerializer.SerializeToUtf8Bytes(torch, SceneFileJsonContext.Default.SceneFileData));
        var scene = new StructureScene(Guid.NewGuid(), AssetPath.Parse(Watchtower).VirtualPath);
        var inner = new StructureScene(Guid.NewGuid(), VirtualPath: null);
        fixture.Structures.Publish(new SceneStructure(
            AssetPath.Parse(Watchtower),
            scene,
            [
                new StructureNode(NodePath.Of(Torch), "Torch", Parent: null, scene, inner, Geometry: null, HasCamera: false),
                new StructureNode(NodePath.Of(Torch, Flame), "Flame", NodePath.Of(Torch), inner, Instance: null, Geometry: null, HasCamera: false),
            ],
            []));
        return fixture;
    }

    private static SceneInstanceComponent InstanceOf(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<SceneInstanceComponent>().Single();

    private static SourceApplication Application(AuthoringFixture fixture) => new(fixture.Project.Context);

    private Task<SceneEditResult> ApplyAsync(AuthoringFixture fixture, NodeId instance, ChangeSelection selection, ApplyTarget target)
        => Application(fixture).ExecuteAsync(fixture.Authoring, instance, selection, target, this.TestContext.CancellationToken);

    private async Task<float> PostXAsync(AuthoringFixture fixture)
    {
        var tower = await SceneFiles.ReadAsync(fixture.Project.Context, SampleScene.TowerScene, this.TestContext.CancellationToken).ConfigureAwait(false);
        return tower!.AllNodes.Single(node => node.Id == SampleScene.Post.Nodes[0]).Transform.LocalPosition.X;
    }

    private Task<byte[]> FileAsync(AuthoringFixture fixture, string path)
        => fixture.Project.Context.FileSystem.File.ReadAllBytesAsync(fixture.Project.PathOf(path).FullPath, this.TestContext.CancellationToken);
}

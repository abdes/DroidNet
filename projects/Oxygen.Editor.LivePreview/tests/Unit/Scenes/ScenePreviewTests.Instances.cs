// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>
/// Checks that the projector has the engine build each scene instance from its source and the scene's changes, and
/// sends only what an edit changes.
/// </summary>
public sealed partial class ScenePreviewTests
{
    private static readonly AssetPath CarModel = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath VanModel = AssetPath.Parse("/Content/Vehicles/Van.glb");

    [TestMethod]
    public async Task SyncedInstance_IsBuiltWithItsChanges()
    {
        using var fixture = new PreviewFixture();
        var wheel = NodePath.Of(NodeId.New());
        var built = new Scene { Name = "Test Scene" };
        var root = new SceneNode(built, "Car");
        built.RootNodes.Add(root);
        root.Components.Add(new SceneInstanceComponent
        {
            Name = "Scene Instance",
            Source = CarModel,
            Changes = InstanceChanges.None
                .With(wheel, InstancedNodeChanges.None
                    .With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, 2f))
                    .With(SourceOverrides.ChangesOf(SceneSchema.Geometry.Kind).With(SceneSchema.Geometry.LodPolicy, new LodPolicy.Fixed(1)))),
        });
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);

        _ = await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false);

        var instance = fixture.World.Commands.OfType<RuntimeInstantiateScene>().Should().ContainSingle().Subject;
        _ = instance.NodeId.Should().Be(root.Id);
        _ = instance.SourcePath.Should().Be(ContentPipelinePaths.ToCookedScenePath(AssetFileTypes.SceneProductOf(CarModel)!), "a model instances its scene product");
        _ = instance.Changes.Should().BeEquivalentTo(
        [
            new RuntimeInstanceChange(wheel, RuntimeOverrideId.TransformPositionX, Guid.Empty, new RuntimeInstanceValue.Scalar(2f)),
            new RuntimeInstanceChange(wheel, RuntimeOverrideId.GeometryLodPolicy, Guid.Empty, new RuntimeInstanceValue.LodPolicyValue(new LodPolicy.Fixed(1))),
        ]);
    }

    [TestMethod]
    public async Task AddedInstance_IsBuilt_AndRemovingItDetachesIt()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();
        var authoring = PreviewFixture.AuthoringOf(document);
        var component = ComponentId.New();

        _ = await authoring.ExecuteAsync(new AddComponent(node.Id, new SceneInstanceComponentData { Id = component, Name = "Scene Instance", Source = CarModel }, index: node.Components.Count)).ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeInstantiateScene>().Should().ContainSingle(command => command.NodeId == node.Id);
        fixture.Requests.Clear();

        _ = await authoring.ExecuteAsync(new RemoveComponent(node.Id, component)).ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeDetachInstance>().Should().ContainSingle(command => command.NodeId == node.Id);
        _ = fixture.World.Commands.OfType<RuntimeDetachGeometry>().Should().BeEmpty("the node keeps its own geometry");
        fixture.Requests.Clear();

        _ = await authoring.UndoAsync().ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeInstantiateScene>().Should().ContainSingle("undoing the removal builds the instance again");
    }

    [TestMethod]
    public async Task SwappedInstance_IsBuiltFromTheNewSource_AndUndoBuildsTheOldOne()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var root = new SceneNode(built, "Car");
        built.RootNodes.Add(root);
        root.Components.Add(new SceneInstanceComponent { Name = "Scene Instance", Source = CarModel });
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();
        var authoring = PreviewFixture.AuthoringOf(document);

        _ = await authoring.ExecuteAsync(new SetInstanceSource(root.Id, VanModel, InstanceSwap.KeepByName)).ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeInstantiateScene>().Should().ContainSingle()
            .Which.SourcePath.Should().Be(ContentPipelinePaths.ToCookedScenePath(AssetFileTypes.SceneProductOf(VanModel)!));
        fixture.Requests.Clear();

        _ = await authoring.UndoAsync().ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeInstantiateScene>().Should().ContainSingle()
            .Which.SourcePath.Should().Be(ContentPipelinePaths.ToCookedScenePath(AssetFileTypes.SceneProductOf(CarModel)!), "undoing the swap builds the old source again");
    }

    [TestMethod]
    public void ChangedInstance_SendsOnlyWhatChanged()
    {
        var root = new SceneNode(new Scene { Name = "Test Scene" }, "Car");
        var body = NodePath.Of(NodeId.New());
        var wheel = NodePath.Of(NodeId.New(), NodeId.New());
        var before = InstanceChanges.None
            .With(body, Moved(1f))
            .With(wheel, Moved(3f));
        var after = InstanceChanges.None
            .With(body, Moved(2f))
            .With(wheel.Append(NodeId.New()), Moved(3f));

        var commands = ScenePreview.ChangeCommands(root, before, after);

        _ = commands.OfType<RuntimeClearInstanceChange>().Should().ContainSingle()
            .Which.Should().Be(new RuntimeClearInstanceChange(root.Id, wheel, RuntimeOverrideId.TransformPositionX, Guid.Empty));
        _ = commands.OfType<RuntimeSetInstanceChange>().Select(static command => command.Change.Value)
            .Should().BeEquivalentTo([new RuntimeInstanceValue.Scalar(2f), new RuntimeInstanceValue.Scalar(3f)], "an unchanged value is not sent again");
        _ = ScenePreview.ChangeCommands(root, after, after).Should().BeEmpty();
    }

    private static InstancedNodeChanges Moved(float x)
        => InstancedNodeChanges.None.With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, x));
}

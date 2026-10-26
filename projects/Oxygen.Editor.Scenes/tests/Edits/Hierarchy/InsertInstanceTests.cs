// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>
/// Placing an instance (scene instances 6.1, 8.3): <see cref="InsertNodes"/> with
/// <see cref="SceneNodeTemplates.Instance"/> creates the instance root named after its source, and refuses a source
/// that is or instances the scene with <see cref="SceneEditCodes.InstanceCycle"/>.
/// </summary>
[TestClass]
public sealed class InsertInstanceTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath Watchtower = AssetPath.Parse("/Content/Watchtower.oscene");

    [TestMethod]
    public async Task AModel_IsPlacedAsARootNamedAfterItsSource()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = SceneNodeTemplates.Instance(fixture.Scene, Car, new NodePlacement(new Vector3(1, 2, 3)));

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var placed = fixture.Node(node.Id);
        _ = placed.Metadata.Name.Should().Be("Car");
        _ = placed.Hierarchy.Parent.Should().BeNull();
        _ = placed.Transform.LocalPosition.Should().Be(new Vector3(1, 2, 3));
        var instance = placed.Components.OfType<SceneInstanceComponent>().Should().ContainSingle().Subject;
        _ = instance.Source.Should().Be(Car);
        _ = instance.Changes.Should().Be(InstanceChanges.None);
    }

    [TestMethod]
    public async Task AnAuthoredScene_IsNamedAfterTheScene()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var node = SceneNodeTemplates.Instance(fixture.Scene, Watchtower);

        _ = node.Name.Should().Be("Watchtower");
        _ = node.Components.OfType<SceneInstanceComponentData>().Should().ContainSingle().Which.Source.Should().Be(Watchtower);
    }

    [TestMethod]
    public async Task ASourceThatIsNeitherAModelNorAScene_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var act = () => SceneNodeTemplates.Instance(fixture.Scene, AssetPath.Parse("/Content/Paint/Red.omat"));

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public async Task Placing_UndoesAndRedoes()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = SceneNodeTemplates.Instance(fixture.Scene, Car);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false));

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Scene.AllNodes.Should().NotContain(candidate => candidate.Id == node.Id);

        SceneAssert.Applied(await fixture.Authoring.RedoAsync().ConfigureAwait(false));
        _ = fixture.Node(node.Id).Components.OfType<SceneInstanceComponent>().Single().Source.Should().Be(Car);
    }

    [TestMethod]
    public async Task TheSceneItself_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = SceneNodeTemplates.Instance(fixture.Scene, SceneAsset(fixture));

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.InstanceCycle, node.Id);
        _ = fixture.Scene.AllNodes.Should().NotContain(candidate => candidate.Id == node.Id);
    }

    [TestMethod]
    public async Task ASceneThatInstancesTheScene_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.References.Set(Watchtower, SceneAsset(fixture));
        var node = SceneNodeTemplates.Instance(fixture.Scene, Watchtower);

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.InstanceCycle, node.Id);
    }

    [TestMethod]
    public async Task AnInstanceInsideAnInsertedSubtree_IsChecked()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var instance = SceneNodeTemplates.Instance(fixture.Scene, SceneAsset(fixture));
        var root = SceneBuilder.NodeData(NodeId.New(), "Root", children: [instance]);

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, root)])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.InstanceCycle, instance.Id);
    }

    [TestMethod]
    public async Task ASceneThatDoesNotInstanceTheScene_IsPlaced()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.References.Set(Watchtower, Car);
        var node = SceneNodeTemplates.Instance(fixture.Scene, Watchtower);

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
    }

    // The fixture opens the scene as /Content/<name>.oscene.
    private static AssetPath SceneAsset(AuthoringFixture fixture) => AssetPath.Parse($"/Content/{fixture.Scene.Name}.oscene");
}

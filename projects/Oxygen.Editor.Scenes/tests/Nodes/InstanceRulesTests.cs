// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// <see cref="InstanceRules"/> (scene instances 6.2, 8.3): a source makes a cycle when it is the scene or reaches it
/// through the references the cook dependency graph follows; a structural command over targets holding a node an
/// instance brought in is refused as a whole with <see cref="SceneEditCodes.InstancedStructure"/>.
/// </summary>
[TestClass]
public sealed class InstanceRulesTests
{
    private static readonly AssetPath Harbor = AssetPath.Parse("/Content/Harbor.oscene");
    private static readonly AssetPath Watchtower = AssetPath.Parse("/Content/Watchtower.oscene");
    private static readonly AssetPath Village = AssetPath.Parse("/Content/Village.oscene");
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath Red = AssetPath.Parse("/Content/Paint/Red.omat");

    [TestMethod]
    public void TheSceneItself_Cycles()
        => _ = InstanceRules.WouldCycle(Harbor, Harbor, References()).Should().BeTrue();

    [TestMethod]
    public void TheSceneByItsDescriptor_Cycles()
        => _ = InstanceRules.WouldCycle(AssetPath.Parse("/Content/Harbor.oscene.json"), Harbor, References()).Should().BeTrue();

    [TestMethod]
    public void ASceneInstancingTheScene_Cycles()
        => _ = InstanceRules.WouldCycle(Harbor, Watchtower, References((Watchtower, [Harbor]))).Should().BeTrue();

    [TestMethod]
    public void ASceneReachingTheSceneThroughAnother_Cycles()
        => _ = InstanceRules.WouldCycle(Harbor, Village, References((Village, [Red, Watchtower]), (Watchtower, [Harbor]))).Should().BeTrue();

    [TestMethod]
    public void ASceneThatDoesNotReachTheScene_DoesNotCycle()
        => _ = InstanceRules.WouldCycle(Harbor, Village, References((Village, [Watchtower]), (Watchtower, [Car]), (Harbor, [Village]))).Should().BeFalse();

    [TestMethod]
    public void AModel_DoesNotCycle()
        => _ = InstanceRules.WouldCycle(Harbor, Car, References((Harbor, [Car]))).Should().BeFalse();

    [TestMethod]
    public void ACycleAmongOtherScenes_Terminates()
        => _ = InstanceRules.WouldCycle(Harbor, Village, References((Village, [Watchtower]), (Watchtower, [Village]))).Should().BeFalse();

    [TestMethod]
    public async Task NodesOfTheScene_AreNotRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);

        _ = InstanceRules.RefusesStructure(fixture.Scene, [NodeReference.To(SampleScene.B), NodeReference.To(root)]).Should().BeNull(
            "the instance root is a node of the scene, and every operation applies to it");
    }

    [TestMethod]
    public async Task ANodeAnInstanceBroughtIn_IsRefusedOnItsInstanceRoot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);
        var door = new NodeReference(root, NodePath.Of(NodeId.New()));

        var refusal = InstanceRules.RefusesStructure(fixture.Scene, [door], node => node == door ? "Door_FL" : null);

        _ = refusal.Should().NotBeNull();
        _ = refusal!.IsError.Should().BeTrue();
        _ = refusal.Code.Should().Be(SceneEditCodes.InstancedStructure);
        _ = refusal.Node.Should().Be(root);
    }

    [TestMethod]
    public async Task ASelectionHoldingAnInstancedNode_IsRefusedWhole()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var root = await PlaceCarAsync(fixture).ConfigureAwait(false);

        var refusal = InstanceRules.RefusesStructure(fixture.Scene, [NodeReference.To(SampleScene.B), new NodeReference(root, NodePath.Of(NodeId.New()))]);

        _ = refusal.Should().NotBeNull();
        _ = refusal!.Code.Should().Be(SceneEditCodes.InstancedStructure);
    }

    private static async Task<NodeId> PlaceCarAsync(AuthoringFixture fixture)
    {
        var node = SceneNodeTemplates.Instance(fixture.Scene, Car);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false));
        return node.Id;
    }

    private static Func<AssetPath, IEnumerable<AssetPath>> References(params (AssetPath Asset, AssetPath[] Referenced)[] edges)
    {
        var graph = edges.ToDictionary(static edge => edge.Asset, static edge => edge.Referenced);
        return asset => graph.GetValueOrDefault(asset, []);
    }
}

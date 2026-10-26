// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// <see cref="InstanceRules.WouldCycle"/> (scene instances 8.3): a source makes a cycle when it is the scene or reaches it
/// through the references the cook dependency graph follows.
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

    private static Func<AssetPath, IEnumerable<AssetPath>> References(params (AssetPath Asset, AssetPath[] Referenced)[] edges)
    {
        var graph = edges.ToDictionary(static edge => edge.Asset, static edge => edge.Referenced);
        return asset => graph.GetValueOrDefault(asset, []);
    }
}

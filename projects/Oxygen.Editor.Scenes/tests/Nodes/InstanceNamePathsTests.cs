// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// A node's name path in an instance's source (scene instances 6.5): one step per node from below the instance root,
/// same-named siblings by their order, nodes of nested instances below their instance root; written in the anchor
/// notation.
/// </summary>
[TestClass]
public sealed class InstanceNamePathsTests
{
    private static readonly NodeId Tower = NodeId.New();
    private static readonly NodeId Torch = NodeId.New();
    private static readonly NodeId SecondTorch = NodeId.New();
    private static readonly NodeId Flame = NodeId.New();

    [TestMethod]
    public void ANestedNode_IsNamedFromBelowTheInstanceRoot_AndFoundAgain()
    {
        var structure = Outpost();
        var flame = NodePath.Of(SecondTorch, Flame);

        var steps = InstanceNamePaths.Of(structure, flame)!;

        _ = steps.Should().Equal(new InstanceNameStep("Tower", 0), new InstanceNameStep("Torch", 1), new InstanceNameStep("Flame", 0));
        _ = InstanceNamePaths.Find(structure, steps).Should().Be(flame);
        _ = InstanceNamePaths.Find(structure, [new("Tower", 0), new("Torch", 2)]).Should().BeNull("there is no third Torch");
        _ = InstanceNamePaths.Of(structure, NodePath.Of(NodeId.New())).Should().BeNull();
    }

    [TestMethod]
    public void Format_NumbersSameNamedSiblingsFromTheSecond_UnnamedFromTheFirst_AndEscapes()
    {
        _ = InstanceNamePaths.Format([new("Body", 0), new("Mirror", 1)]).Should().Be("Body/Mirror[2]");
        _ = InstanceNamePaths.Format([new("Body", 0), new(string.Empty, 0)]).Should().Be("Body/[1]");
        _ = InstanceNamePaths.Format([new("A/B[#]\\", 0)]).Should().Be("A\\/B\\[\\#]\\\\");
    }

    // Tower holds two Torches; the second instances a scene that brings in Flame.
    private static SceneStructure Outpost()
    {
        var source = AssetPath.Parse("/Content/Outpost.oscene");
        var scene = new StructureScene(Guid.NewGuid(), source.VirtualPath);
        var torch = new StructureScene(Guid.NewGuid(), VirtualPath: null);
        return new SceneStructure(
            source,
            scene,
            [
                new StructureNode(NodePath.Of(Tower), "Tower", Parent: null, scene, Instance: null, Geometry: null, HasCamera: false),
                new StructureNode(NodePath.Of(Torch), "Torch", NodePath.Of(Tower), scene, Instance: null, Geometry: null, HasCamera: false),
                new StructureNode(NodePath.Of(SecondTorch), "Torch", NodePath.Of(Tower), scene, torch, Geometry: null, HasCamera: false),
                new StructureNode(NodePath.Of(SecondTorch, Flame), "Flame", NodePath.Of(SecondTorch), torch, Instance: null, Geometry: null, HasCamera: false),
            ],
            []);
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

/// <summary>
/// The node Rendering section appears only where its flags affect the node: Scene Visibility for
/// geometry and lights, the shadow flags for geometry.
/// </summary>
[TestClass]
public sealed class NodeRenderingApplicabilityTests
{
    [TestMethod]
    public void Section_DoesNotApplyToCamerasOrEmptyNodes()
    {
        using var fixture = new SceneAuthoringFixture();
        var camera = CreateNode(fixture, "Camera", new PerspectiveCamera { Name = "Camera" });
        var empty = CreateNode(fixture, "Group");

        _ = NodeRenderingViewModel.AppliesTo([camera]).Should().BeFalse();
        _ = NodeRenderingViewModel.AppliesTo([empty]).Should().BeFalse();
        _ = NodeRenderingViewModel.AppliesTo([]).Should().BeFalse();
    }

    [TestMethod]
    public void Section_AppliesToLightsWithoutShadowFlags()
    {
        using var fixture = new SceneAuthoringFixture();
        var light = CreateNode(fixture, "Lamp", new PointLightComponent { Name = "Point Light", Range = 10f });
        using var model = new NodeRenderingViewModel();

        model.UpdateValues([light]);

        _ = NodeRenderingViewModel.AppliesTo([light]).Should().BeTrue();
        _ = model.ShowsShadowFlags.Should().BeFalse("light shadows are authored on the light, not on the node");
    }

    [TestMethod]
    public void Section_ShowsShadowFlagsOnlyWhenEverySelectedNodeHasGeometry()
    {
        using var fixture = new SceneAuthoringFixture();
        var cube = CreateNode(fixture, "Cube", new GeometryComponent { Name = "Geometry" });
        var light = CreateNode(fixture, "Lamp", new PointLightComponent { Name = "Point Light", Range = 10f });
        var camera = CreateNode(fixture, "Camera", new PerspectiveCamera { Name = "Camera" });
        using var model = new NodeRenderingViewModel();

        model.UpdateValues([cube]);
        _ = model.ShowsShadowFlags.Should().BeTrue();

        model.UpdateValues([cube, light]);
        _ = model.ShowsShadowFlags.Should().BeFalse();
        _ = NodeRenderingViewModel.AppliesTo([cube, light]).Should().BeTrue();

        _ = NodeRenderingViewModel.AppliesTo([cube, camera]).Should().BeFalse();
    }

    private static SceneNode CreateNode(SceneAuthoringFixture fixture, string name, GameComponent? component = null)
    {
        var node = new SceneNode(fixture.Scene) { Name = name };
        if (component is not null)
        {
            node.Components.Add(component);
        }

        fixture.Scene.RootNodes.Add(node);
        return node;
    }
}

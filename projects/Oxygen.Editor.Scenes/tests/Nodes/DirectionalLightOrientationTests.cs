// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// <see cref="DirectionalLightOrientation"/>: the direction a light shines from, read from and written to its node's
/// rotation.
/// </summary>
[TestClass]
public sealed class DirectionalLightOrientationTests
{
    [TestMethod]
    [DataRow(0f, 45f)]
    [DataRow(30f, 10f)]
    [DataRow(-90f, -20f)]
    public void LocalRotation_OfARootLight_ShinesFromTheGivenDirection(float azimuth, float elevation)
    {
        var light = new SceneNode(new Scene { Name = "Orientation" }, "Light");

        Rotate(light, DirectionalLightOrientation.LocalRotation(light, azimuth, elevation));

        AssertShinesFrom(light, azimuth, elevation);
    }

    [TestMethod]
    public void LocalRotation_OfAChildLight_CompensatesForItsParent()
    {
        var scene = new Scene { Name = "Orientation" };
        var parent = new SceneNode(scene, "Parent");
        var light = new SceneNode(scene, "Light");
        parent.Hierarchy.AddChild(light);
        Rotate(parent, Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI / 2f));

        var local = DirectionalLightOrientation.LocalRotation(light, 30f, 10f);
        Rotate(light, local);

        AssertShinesFrom(light, 30f, 10f);
        var unparented = DirectionalLightOrientation.LocalRotation(new SceneNode(scene, "Root light"), 30f, 10f);
        _ = MathF.Abs(Quaternion.Dot(local, unparented)).Should().BeLessThan(0.999f, "the parent's turn is undone in the local rotation");
    }

    [TestMethod]
    public void LocalRotation_OfALightThatIgnoresItsParent_IsItsWorldRotation()
    {
        var scene = new Scene { Name = "Orientation" };
        var parent = new SceneNode(scene, "Parent");
        var light = new SceneNode(scene, "Light");
        light.Flags.IgnoreParentTransform = true;
        parent.Hierarchy.AddChild(light);
        Rotate(parent, Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI / 2f));

        Rotate(light, DirectionalLightOrientation.LocalRotation(light, 30f, 10f));

        AssertShinesFrom(light, 30f, 10f);
    }

    private static void Rotate(SceneNode node, Quaternion rotation)
        => node.Transform.LocalRotation = rotation;

    private static void AssertShinesFrom(SceneNode light, float azimuth, float elevation)
    {
        var (shownAzimuth, shownElevation) = DirectionalLightOrientation.DisplayAngles(light);
        _ = shownAzimuth.Should().BeApproximately(azimuth, 0.01f);
        _ = shownElevation.Should().BeApproximately(elevation, 0.01f);
    }
}

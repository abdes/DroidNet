// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>
/// The values of a node an instance brings in come from the engine's read of the mounted descriptors (scene instances
/// 8.5), each property's value converted to the type the scene schema gives it.
/// </summary>
[TestClass]
public sealed class ScenePreviewNodeValuesTests
{
    private const string Revision = "0101010101010101010101010101010101010101010101010101010101010101";

    private static readonly Guid Rim = Guid.NewGuid();

    /// <summary>
    /// Scalars, vectors, enums by wire value, assets by virtual path and LOD policies each take their schema type; the
    /// geometry becomes an asset path and a surface value keeps its part and layout revision.
    /// </summary>
    [TestMethod]
    public void EachValueTakesItsSchemaType()
    {
        var policy = new LodPolicy.Distance([10f, 20f], 0.1f);
        var values = new RuntimeSceneNodeValues(
            Exists: true,
            "Wheel",
            "/Content/Car.glb/Wheel.ogeo",
            [
                Property(RuntimeOverrideId.TransformPositionX, new RuntimeInstanceValue.Scalar(2f)),
                Property(RuntimeOverrideId.TransformRotation, new RuntimeInstanceValue.Vector(new Vector3(0f, 90f, 0f))),
                Property(RuntimeOverrideId.NodeVisibility, new RuntimeInstanceValue.Integer((int)VisibilitySource.Hidden)),
                Property(RuntimeOverrideId.NodeIsStatic, new RuntimeInstanceValue.Flag(Value: true)),
                Property(RuntimeOverrideId.DirectionalLightCascadeCount, new RuntimeInstanceValue.Integer(3)),
                new RuntimeSceneNodeProperty(RuntimeOverrideId.GeometrySurfaceMaterial, Rim, Revision, new RuntimeInstanceValue.Asset("/Content/Car.glb/Paint.omat")),
                Property(RuntimeOverrideId.GeometryLodPolicy, new RuntimeInstanceValue.LodPolicyValue(policy)),
            ]);

        var read = ScenePreview.ToNodeValues(values);

        _ = read.Name.Should().Be("Wheel");
        _ = read.Geometry.Should().Be(AssetPath.Parse("/Content/Car.glb/Wheel.ogeo"));
        var byProperty = read.Values.ToDictionary(static value => value.Property);
        _ = byProperty[SceneSchema.Transform.PositionX.Id].Value.Should().Be(2f);
        _ = byProperty[SceneSchema.Transform.Rotation.Id].Value.Should().Be(new Vector3(0f, 90f, 0f));
        _ = byProperty[SceneSchema.Node.Visibility.Id].Value.Should().Be(VisibilitySource.Hidden);
        _ = byProperty[SceneSchema.Node.IsStatic.Id].Value.Should().Be(expected: true);
        _ = byProperty[SceneSchema.DirectionalLight.CascadeCount.Id].Value.Should().Be(3);
        _ = byProperty[SceneSchema.Geometry.LodPolicy.Id].Value.Should().Be(policy);
        var paint = byProperty[SceneSchema.Geometry.Surface.Material.Id];
        _ = paint.Value.Should().Be(AssetPath.Parse("/Content/Car.glb/Paint.omat"));
        _ = paint.Part.Should().Be(Rim);
        _ = paint.LayoutRevision.Should().Be(Revision);
    }

    /// <summary>A node that renders a geometry no mounted content provides has no geometry; one that renders none neither.</summary>
    /// <param name="geometryPath">The geometry path the engine read.</param>
    [TestMethod]
    [DataRow("")]
    [DataRow(null)]
    public void AnUnresolvedGeometryIsNone(string? geometryPath)
    {
        var read = ScenePreview.ToNodeValues(new RuntimeSceneNodeValues(Exists: true, "Body", geometryPath, []));

        _ = read.Geometry.Should().BeNull();
    }

    /// <summary>A value whose kind is not the property's schema type is a broken read, not a value.</summary>
    [TestMethod]
    public void AValueOfAnotherTypeIsRefused()
    {
        var values = new RuntimeSceneNodeValues(
            Exists: true,
            "Wheel",
            GeometryPath: null,
            [Property(RuntimeOverrideId.TransformPositionX, new RuntimeInstanceValue.Flag(Value: true))]);

        var read = () => ScenePreview.ToNodeValues(values);

        _ = read.Should().Throw<InvalidOperationException>();
    }

    private static RuntimeSceneNodeProperty Property(RuntimeOverrideId id, RuntimeInstanceValue value)
        => new(id, Guid.Empty, LayoutRevision: null, value);
}

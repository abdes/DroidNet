// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// <see cref="SceneNodeTemplates"/> (scene-edits.md 4): the default nodes of the toolbar, as data with new ids.
/// </summary>
[TestClass]
public sealed class SceneNodeTemplatesTests
{
    public static IEnumerable<TestDataRow<PrimitiveKind>> Primitives =>
        [.. Enum.GetValues<PrimitiveKind>().Select(static kind => new TestDataRow<PrimitiveKind>(kind) { DisplayName = kind.ToString() })];

    public static IEnumerable<TestDataRow<LightTransformCase>> LightTransforms =>
    [
        new(new LightTransformCase(LightKind.Directional, Vector3.Zero, DirectionalLightComponent.DefaultLocalRotation)) { DisplayName = "Directional" },
        new(new LightTransformCase(LightKind.Point, new Vector3(0, 2, 0), Quaternion.Identity)) { DisplayName = "Point" },
        new(new LightTransformCase(LightKind.Spot, new Vector3(0, 3, 3), Quaternion.CreateFromAxisAngle(Vector3.UnitX, -MathF.PI / 4))) { DisplayName = "Spot" },
    ];

    public static IEnumerable<TestDataRow<AppendCase>> Scopes =>
    [
        new(new AppendCase(Parent: null, Folder: null)) { DisplayName = "Root" },
        new(new AppendCase(SampleScene.A, Folder: null)) { DisplayName = "NodeA" },
        new(new AppendCase(Parent: null, SampleScene.F)) { DisplayName = "FolderF" },
    ];

    [TestMethod]
    [DynamicData(nameof(Primitives))]
    public async Task Primitive(PrimitiveKind kind)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var node = SceneNodeTemplates.Primitive(fixture.Scene, kind);

        _ = node.Name.Should().Be(kind.ToString());
        var geometry = node.Components.OfType<GeometryComponentData>().Should().ContainSingle().Which;
        _ = geometry.GeometryUri.VirtualPath.Should().Be($"/Engine/Generated/BasicShapes/{kind}.ogeo");
        AssertNewIds(fixture, node);
    }

    [TestMethod]
    public async Task Primitive_UndefinedKind_ThrowsArgumentOutOfRange()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var act = () => SceneNodeTemplates.Primitive(fixture.Scene, (PrimitiveKind)999);

        _ = act.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    [DynamicData(nameof(LightTransforms))]
    public async Task Light_DefaultTransform(LightTransformCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var node = SceneNodeTemplates.Light(fixture.Scene, row.Kind);

        var transform = node.Components.OfType<TransformData>().Single();
        _ = transform.Position.Should().Be(row.Position);
        SceneAssert.SameRotation(transform.Rotation, row.Rotation);
    }

    [TestMethod]
    public async Task Light_UndefinedKind_ThrowsArgumentOutOfRange()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var act = () => SceneNodeTemplates.Light(fixture.Scene, (LightKind)999);

        _ = act.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    [DataRow(CameraKind.Perspective, typeof(PerspectiveCameraData), DisplayName = "Perspective")]
    [DataRow(CameraKind.Orthographic, typeof(OrthographicCameraData), DisplayName = "Orthographic")]
    public async Task Camera(CameraKind kind, Type component)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var node = SceneNodeTemplates.Camera(fixture.Scene, kind);

        _ = node.Name.Should().Be("Camera");
        _ = node.Components.OfType<CameraComponentData>().Should().ContainSingle().Which.Should().BeOfType(component);
    }

    [TestMethod]
    public async Task Placement_OverridesTheDefaultPositionAndRotation_KeepsScale()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var rotation = Quaternion.CreateFromAxisAngle(Vector3.UnitY, 1f);

        var node = SceneNodeTemplates.Light(fixture.Scene, LightKind.Spot, new NodePlacement(new Vector3(5, 0, 0), rotation));

        var transform = node.Components.OfType<TransformData>().Single();
        _ = transform.Position.Should().Be(new Vector3(5, 0, 0));
        SceneAssert.SameRotation(transform.Rotation, rotation);
        _ = transform.Scale.Should().Be(Vector3.One);
    }

    [TestMethod]
    public async Task TwoCalls_ReturnDifferentIds()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var first = SceneNodeTemplates.Primitive(fixture.Scene, PrimitiveKind.Cube);
        var second = SceneNodeTemplates.Primitive(fixture.Scene, PrimitiveKind.Cube);

        _ = first.Id.Should().NotBe(second.Id);
        _ = first.Components.Select(static component => component.Id).Should().NotIntersectWith(second.Components.Select(static component => component.Id));
    }

    [TestMethod]
    [DynamicData(nameof(Scopes))]
    public async Task Append_PutsTheNodeAtTheEndOfItsScope(AppendCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = SceneNodeTemplates.Empty(fixture.Scene, "Appended");
        var insert = SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node, row.Parent, row.Folder);
        var container = row.Folder is { } folder
            ? LayoutPosition.InFolder(folder, 0)
            : row.Parent is { } parent ? LayoutPosition.UnderNode(parent, 0) : LayoutPosition.AtRoot(0);

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([insert])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var siblings = row.Parent is { } owner ? fixture.Node(owner).Hierarchy.Children : fixture.Scene.RootNodes;
        _ = siblings[^1].Id.Should().Be(node.Id);
        _ = LayoutScopes.EntriesOf(fixture.Layout, container)[^1].Should().Be(LayoutEntryId.Of(node.Id));
    }

    // Scene model: a new root starts Shown, On and On, a new child Inherit for all three.
    [TestMethod]
    [DynamicData(nameof(Scopes))]
    public async Task Append_GivesTheNodeTheSourceModesOfANewRootOrChild(AppendCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = SceneNodeTemplates.Empty(fixture.Scene, "Appended");

        var result = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node, row.Parent, row.Folder)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var flags = fixture.Node(node.Id).Flags;
        var isRoot = row.Parent is null;
        _ = flags.Visibility.Should().Be(isRoot ? VisibilitySource.Shown : VisibilitySource.Inherit);
        _ = flags.CastsShadows.Should().Be(isRoot ? ShadowSource.On : ShadowSource.Inherit);
        _ = flags.ReceivesShadows.Should().Be(isRoot ? ShadowSource.On : ShadowSource.Inherit);
    }

    // Created nodes are new: no id of the node or its components is the scene's, and none is empty.
    private static void AssertNewIds(AuthoringFixture fixture, SceneNodeData node)
    {
        var sceneNodes = fixture.Scene.AllNodes.Select(static scene => scene.Id).ToList();
        var sceneComponents = fixture.Scene.AllNodes.SelectMany(static scene => scene.Components).Select(static component => component.Id).ToList();
        _ = node.Id.IsEmpty.Should().BeFalse();
        _ = sceneNodes.Should().NotContain(node.Id);
        var ids = node.Components.Select(static component => component.Id).ToList();
        _ = ids.Should().OnlyHaveUniqueItems().And.NotContain(sceneComponents);
    }
}

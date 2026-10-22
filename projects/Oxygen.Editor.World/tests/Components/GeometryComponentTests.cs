// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies <see cref="GeometryComponent"/> asset reference persistence and the overrides it stores over its geometry's
/// surfaces.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(GeometryComponent))]
public sealed class GeometryComponentTests
{
    private static readonly AssetPath Cube = AssetPath.Parse("/Engine/Generated/BasicShapes/Cube");
    private static readonly AssetPath Stone = AssetPath.Parse("/Content/Materials/Stone.omat");
    private static readonly PartId Top = new(SceneSchema.Geometry.Surface.Kind, Guid.Parse("30000000-0000-0000-0000-000000000001"));
    private static readonly string Revision = new('a', 64);

    [TestMethod]
    public void Overrides_SurviveDehydrateAndHydrate()
    {
        var scene = new Scene { Name = "Geometry Scene" };
        var node = new SceneNode(scene, "MeshNode");
        scene.RootNodes.Add(node);
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = Cube };
        _ = node.AddComponent(geometry);
        geometry.Overrides = geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, Top, Revision, Stone);

        var restored = new Scene { Name = scene.Name, Id = scene.Id };
        restored.Hydrate(scene.Dehydrate());

        var overrides = restored.RootNodes.Single().Components.OfType<GeometryComponent>().Single().Overrides;
        _ = overrides.Should().Be(geometry.Overrides);
        _ = overrides.TryGet(SceneSchema.Geometry.Surface.Material, Top, out var material).Should().BeTrue();
        _ = material.Should().Be(Stone);
    }

    [TestMethod]
    public void Overrides_AreSavedAsTheValuesOfEachPart()
    {
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = Cube };
        geometry.Overrides = geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, Top, Revision, Stone);

        var json = JsonNode.Parse(JsonSerializer.Serialize(geometry.Dehydrate(), SceneJsonContext.Default.ComponentData))!;

        _ = JsonNode.DeepEquals(json["Overrides"], JsonNode.Parse($$"""
            { "Parts": [ { "Kind": "surface", "Id": "{{Top.Id}}", "LayoutRevision": "{{Revision}}", "Values": { "/material": "{{Stone.ToUriString()}}" } } ] }
            """)).Should().BeTrue();
    }

    [TestMethod]
    public void NoOverrides_AreNotSaved()
    {
        var json = JsonNode.Parse(JsonSerializer.Serialize(new GeometryComponent { Name = "Geometry", Geometry = Cube }.Dehydrate(), SceneJsonContext.Default.ComponentData))!;

        _ = json.AsObject().ContainsKey("Overrides").Should().BeFalse();
    }

    [TestMethod]
    public void Overrides_OfAnotherComponentKind_AreRefused()
    {
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = Cube };

        var act = () => geometry.Overrides = SourceOverrides.For(SceneSchema.PointLight.Kind);

        _ = act.Should().Throw<ArgumentException>();
        _ = geometry.Overrides.ComponentKind.Should().Be(SceneSchema.Geometry.Kind);
    }

    [TestMethod]
    public void NewGeometry_KeepsTheOverrides()
    {
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = Cube };
        geometry.Overrides = geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, Top, Revision, Stone);

        geometry.Geometry = AssetPath.Parse("/Content/Meshes/Pillar.ogeo");

        _ = geometry.Overrides.IsEmpty.Should().BeFalse("an override of a surface the new geometry lacks stays, unresolved");
    }

    [TestMethod]
    [DataRow("""{ "Parts": [ { "Kind": "surface", "Id": "30000000-0000-0000-0000-000000000001", "LayoutRevision": "REVISION", "Values": { "/shade": "asset:///Content/Materials/Stone.omat" } } ] }""", DisplayName = "An unknown key")]
    [DataRow("""{ "Parts": [ { "Kind": "section", "Id": "30000000-0000-0000-0000-000000000001", "LayoutRevision": "REVISION", "Values": { "/material": "asset:///Content/Materials/Stone.omat" } } ] }""", DisplayName = "An unknown part kind")]
    [DataRow("""{ "Parts": [ { "Kind": "surface", "Id": "30000000-0000-0000-0000-000000000001", "LayoutRevision": "REVISION", "Values": { "/material": 3 } } ] }""", DisplayName = "A value of another type")]
    [DataRow("""{ "Parts": [ { "Kind": "surface", "Id": "30000000-0000-0000-0000-000000000001", "LayoutRevision": "rev-1", "Values": { "/material": "asset:///Content/Materials/Stone.omat" } } ] }""", DisplayName = "An invalid layout revision")]
    [DataRow("""{ "Parts": [ { "Kind": "surface", "Id": "00000000-0000-0000-0000-000000000000", "LayoutRevision": "REVISION", "Values": { "/material": "asset:///Content/Materials/Stone.omat" } } ] }""", DisplayName = "An empty part id")]
    [DataRow("""{ "Parts": [ { "Kind": "surface", "Id": "30000000-0000-0000-0000-000000000001", "LayoutRevision": "REVISION", "Values": { } } ] }""", DisplayName = "A part without values")]
    [DataRow("""{ "Values": { "/material": "asset:///Content/Materials/Stone.omat" } }""", DisplayName = "A part property without its part")]
    [DataRow("""{ "Values": { "/geometry_uri": "asset:///Content/Meshes/Pillar.ogeo" } }""", DisplayName = "A property without a source")]
    public async Task Deserialize_InvalidOverrides_ThrowsJsonException(string overrides)
    {
        var scene = new Scene { Name = "Crates" };
        var node = new SceneNode(scene, "Crate");
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = Cube });
        scene.RootNodes.Add(node);
        var saved = await SceneTestSupport.SerializeToStreamAsync(scene).ConfigureAwait(false);
        await using var savedLifetime = saved.ConfigureAwait(false);
        var json = JsonNode.Parse(saved.ToArray())!;
        var geometry = json["RootNodes"]![0]!["Components"]!.AsArray().Single(component => (string?)component!["$type"] == "GeometryComponent")!;
        geometry["Overrides"] = JsonNode.Parse(overrides.Replace("REVISION", Revision, StringComparison.Ordinal));
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json.ToJsonString()));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow("OverrideSlots", DisplayName = "Removed component override slots")]
    [DataRow("TargetedOverrides", DisplayName = "Removed LOD and submesh overrides")]
    public async Task Deserialize_RemovedMember_ThrowsJsonException(string member)
    {
        var scene = new Scene { Name = "Crates" };
        var node = new SceneNode(scene, "Crate");
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = Cube });
        scene.RootNodes.Add(node);
        var saved = await SceneTestSupport.SerializeToStreamAsync(scene).ConfigureAwait(false);
        await using var savedLifetime = saved.ConfigureAwait(false);
        var json = JsonNode.Parse(saved.ToArray())!;
        var geometry = json["RootNodes"]![0]!["Components"]!.AsArray().Single(component => (string?)component!["$type"] == "GeometryComponent")!;
        geometry[member] = new JsonArray();
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json.ToJsonString()));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>("a stale scene fails to open instead of losing its overrides on save").ConfigureAwait(false);
    }

    [TestMethod]
    public void Geometry_SetToNull_Throws()
    {
        var component = new GeometryComponent { Name = "Geometry", Geometry = Cube };

        var act = () => component.Geometry = null!;

        _ = act.Should().Throw<ArgumentNullException>();
        _ = component.Geometry.Should().Be(Cube);
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("Content/Crate.ogeo")]
    [DataRow("https://example.com/Content/Crate.ogeo")]
    [DataRow("asset://Content/Crate.ogeo")]
    public async Task Deserialize_GeometryUriNotAnAsset_ThrowsJsonException(string value)
    {
        var scene = new Scene { Name = "Crates" };
        var node = new SceneNode(scene, "Crate");
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = Cube });
        scene.RootNodes.Add(node);
        var saved = await SceneTestSupport.SerializeToStreamAsync(scene).ConfigureAwait(false);
        await using var savedLifetime = saved.ConfigureAwait(false);
        var json = JsonNode.Parse(saved.ToArray())!;
        var geometry = json["RootNodes"]![0]!["Components"]!.AsArray().Single(component => (string?)component!["$type"] == "GeometryComponent")!;
        geometry["GeometryUri"] = value;
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json.ToJsonString()));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Deserialize_GeometryWithoutGeometryUri_ThrowsJsonException()
    {
        var scene = new Scene { Name = "Crates" };
        var node = new SceneNode(scene, "Crate");
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = Cube });
        scene.RootNodes.Add(node);
        var saved = await SceneTestSupport.SerializeToStreamAsync(scene).ConfigureAwait(false);
        await using var savedLifetime = saved.ConfigureAwait(false);
        var json = JsonNode.Parse(saved.ToArray())!;
        var geometry = json["RootNodes"]![0]!["Components"]!.AsArray().Single(component => (string?)component!["$type"] == "GeometryComponent")!;
        _ = geometry.AsObject().Remove("GeometryUri");
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json.ToJsonString()));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>().ConfigureAwait(false);
    }

    [TestMethod]
    public void Hydrate_RejectedGeometry_LeavesTheSceneAsItWas()
    {
        var scene = new Scene { Name = "Crates" };
        var kept = new SceneNode(scene, "Kept");
        scene.RootNodes.Add(kept);
        var data = new SceneData
        {
            Name = "Crates",
            Id = scene.Id,
            RootNodes =
            [
                SceneTestSupport.NodeData("Rejected", new GeometryComponentData { Name = "Geometry", GeometryUri = null! }),
            ],
        };

        var act = () => scene.Hydrate(data);

        _ = act.Should().Throw<JsonException>();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(kept);
    }
}

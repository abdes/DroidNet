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
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;
using MaterialsSlot = Oxygen.Editor.World.Slots.MaterialsSlot;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies <see cref="GeometryComponent"/> asset reference persistence, component override slots, and targeted overrides.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(GeometryComponent))]
public sealed class GeometryComponentTests
{
    private static readonly Uri Cube = new("asset:///Engine/Generated/BasicShapes/Cube");

    [TestMethod]
    public void GeometryComponent_Targeted_And_Component_Overrides_Are_Preserved_InMemory()
    {
        // Arrange
        var scene = new Scene { Name = "Geometry Scene" };
        var node = new SceneNode(scene) { Name = "MeshNode" };
        scene.RootNodes.Add(node);

        var geo = new GeometryComponent
        {
            Name = "HeroGeometry",
            Node = node,
            Geometry = new Uri("asset:///Engine/Generated/BasicShapes/Cube"),
        };

        // component-level override
        var compMat = new MaterialsSlot
        {
            Target = new MaterialSlotTarget(geo.Geometry, Guid.Parse("10000000-0000-0000-0000-000000000001"), "rev-1"),
            Material = new Uri("asset:///Engine/Generated/Materials/Default"),
        };
        geo.OverrideSlots.Add(compMat);

        // targeted override for LOD 0, submesh 1
        var target = new GeometryOverrideTarget { LodIndex = 0, SubmeshIndex = 1 };
        var mat = new MaterialsSlot
        {
            Target = new MaterialSlotTarget(geo.Geometry, Guid.Parse("10000000-0000-0000-0000-000000000002"), "rev-1"),
            Material = new Uri("asset:///Engine/Generated/Materials/Gold"),
        };
        target.OverrideSlots.Add(mat);
        geo.TargetedOverrides.Add(target);

        node.Components.Add(geo);

        // Act: all operations performed in-memory (unit test) — no JSON serialization
        var dto = scene.Dehydrate();
        var restored = new Scene { Name = dto.Name, Id = dto.Id };
        restored.Hydrate(dto);

        // Assert we have the same in-memory structure
        _ = restored.RootNodes.Should().ContainSingle();
        var rnode = restored.RootNodes[0];
        var rgeo = rnode.Components.OfType<GeometryComponent>().Single();

        _ = rgeo.Should().NotBeNull();
        _ = rgeo.Geometry.Should().Be("asset:///Engine/Generated/BasicShapes/Cube");
        _ = rgeo.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle();
        _ = rgeo.OverrideSlots.OfType<MaterialsSlot>().First().Material.Should().Be("asset:///Engine/Generated/Materials/Default");

        _ = rgeo.TargetedOverrides.Should().ContainSingle();
        var rt = rgeo.TargetedOverrides[0];
        _ = rt.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle();
        _ = rt.OverrideSlots.OfType<MaterialsSlot>().First().Material.Should().Be("asset:///Engine/Generated/Materials/Gold");
    }

    [TestMethod]
    public void GeometryComponent_FullHydrateAndDehydrateRoundTripThroughDataRecords_PreservesAllOverrides()
    {
        var source = new GeometryComponentData
        {
            Name = "Pillar",
            GeometryUri = new Uri("asset:///Content/Meshes/Pillar.geometry"),
            OverrideSlots =
            [
                new MaterialsSlotData
                {
                    GeometryUri = new Uri("asset:///Content/Meshes/Pillar.geometry"),
                    SlotId = Guid.Parse("30000000-0000-0000-0000-000000000001"),
                    LayoutRevision = "rev-pillar-1",
                    MaterialUri = new Uri("asset:///Content/Materials/Stone.omat.json"),
                },
            ],
            TargetedOverrides =
            [
                new TargetedOverrideData
                {
                    LodIndex = 1,
                    SubmeshIndex = 0,
                    OverrideSlots =
                    [
                        new MaterialsSlotData
                        {
                            GeometryUri = new Uri("asset:///Content/Meshes/Pillar.geometry"),
                            SlotId = Guid.Parse("30000000-0000-0000-0000-000000000002"),
                            LayoutRevision = "rev-pillar-1",
                            MaterialUri = new Uri("asset:///Content/Materials/MossyStone.omat.json"),
                        },
                    ],
                },
            ],
        };

        var component = (GeometryComponent)GameComponent.CreateAndHydrate(source);
        var dehydrated = (GeometryComponentData)component.Dehydrate();

        _ = component.Name.Should().Be("Pillar");
        _ = component.Geometry.Should().Be("asset:///Content/Meshes/Pillar.geometry");
        _ = component.OverrideSlots.Should().ContainSingle();
        _ = component.TargetedOverrides.Should().ContainSingle();
        _ = dehydrated.Should().BeEquivalentTo(source);
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
    [DataRow("https://example.com/Content/Crate.ogeo")]
    [DataRow("file:///C:/Content/Crate.ogeo")]
    [DataRow("asset:///")]
    public void Geometry_SetToANonAssetUri_ThrowsAndKeepsTheGeometry(string value)
    {
        var component = new GeometryComponent { Name = "Geometry", Geometry = Cube };

        var act = () => component.Geometry = new Uri(value);

        _ = act.Should().Throw<ArgumentException>();
        _ = component.Geometry.Should().Be(Cube);
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("Content/Crate.ogeo")]
    [DataRow("https://example.com/Content/Crate.ogeo")]
    public void CreateAndHydrate_GeometryUriNotAnAsset_ThrowsJsonException(string value)
    {
        var data = new GeometryComponentData { Name = "Geometry", GeometryUri = new Uri(value, UriKind.RelativeOrAbsolute) };

        var act = () => GameComponent.CreateAndHydrate(data);

        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public async Task Deserialize_GeometryWithoutGeometryUri_ThrowsJsonException()
    {
        var scene = new Scene { Name = "Crates" };
        var node = new SceneNode(scene) { Name = "Crate" };
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
        var kept = new SceneNode(scene) { Name = "Kept" };
        scene.RootNodes.Add(kept);
        var data = new SceneData
        {
            Name = "Crates",
            Id = scene.Id,
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Name = "Rejected",
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new GeometryComponentData { Name = "Geometry", GeometryUri = new Uri("https://example.com/Content/Crate.ogeo") },
                    ],
                },
            ],
        };

        var act = () => scene.Hydrate(data);

        _ = act.Should().Throw<JsonException>();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(kept);
    }
}

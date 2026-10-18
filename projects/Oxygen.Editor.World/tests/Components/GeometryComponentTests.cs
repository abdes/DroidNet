// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
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
            Geometry = new("asset:///Engine/Generated/BasicShapes/Cube"),
        };

        // component-level override
        var compMat = new MaterialsSlot();
        compMat.Material.Uri = new("asset:///Engine/Generated/Materials/Default");
        geo.OverrideSlots.Add(compMat);

        // targeted override for LOD 0, submesh 1
        var target = new GeometryOverrideTarget { LodIndex = 0, SubmeshIndex = 1 };
        var mat = new MaterialsSlot();
        mat.Material.Uri = new("asset:///Engine/Generated/Materials/Gold");
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
        _ = rgeo.Geometry!.Uri.Should().Be("asset:///Engine/Generated/BasicShapes/Cube");
        _ = rgeo.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle();
        _ = rgeo.OverrideSlots.OfType<MaterialsSlot>().First().Material.Uri.Should().Be("asset:///Engine/Generated/Materials/Default");

        _ = rgeo.TargetedOverrides.Should().ContainSingle();
        var rt = rgeo.TargetedOverrides[0];
        _ = rt.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle();
        _ = rt.OverrideSlots.OfType<MaterialsSlot>().First().Material.Uri.Should().Be("asset:///Engine/Generated/Materials/Gold");
    }

    [TestMethod]
    public void GeometryComponent_FullHydrateAndDehydrateRoundTripThroughDataRecords_PreservesAllOverrides()
    {
        var source = new GeometryComponentData
        {
            Name = "Pillar",
            GeometryUri = "asset:///Content/Meshes/Pillar.geometry",
            OverrideSlots =
            [
                new MaterialsSlotData
                {
                    GeometryUri = new Uri("asset:///Content/Meshes/Pillar.geometry"),
                    SlotId = Guid.Parse("30000000-0000-0000-0000-000000000001"),
                    LayoutRevision = "rev-pillar-1",
                    MaterialUri = "asset:///Content/Materials/Stone.omat.json",
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
                            MaterialUri = "asset:///Content/Materials/MossyStone.omat.json",
                        },
                    ],
                },
            ],
        };

        var component = (GeometryComponent)GameComponent.CreateAndHydrate(source);
        var dehydrated = (GeometryComponentData)component.Dehydrate();

        _ = component.Name.Should().Be("Pillar");
        _ = component.Geometry!.Uri.Should().Be("asset:///Content/Meshes/Pillar.geometry");
        _ = component.OverrideSlots.Should().ContainSingle();
        _ = component.TargetedOverrides.Should().ContainSingle();
        _ = dehydrated.Should().BeEquivalentTo(source);
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Tests.Slots;

/// <summary>
/// Verifies material override slot target preservation and serialization stability.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(MaterialsSlot))]
public sealed class MaterialsSlotTests
{
    [TestMethod]
    public void MaterialsSlot_RoundTrip_KeepsGeometryUriSlotIdLayoutRevisionAndMaterialUri()
    {
        var target = new MaterialSlotTarget(
            new Uri("asset:///Content/Meshes/Gate.geometry"),
            Guid.Parse("40000000-0000-0000-0000-000000000001"),
            "rev-gate-42");

        var slot = new MaterialsSlot
        {
            Target = target,
            Material = new Uri("asset:///Content/Materials/Iron.omat.json"),
        };

        var dto = (MaterialsSlotData)slot.Dehydrate();
        var restored = (MaterialsSlot)OverrideSlot.CreateAndHydrate(dto);

        _ = restored.Target.GeometryUri.Should().Be(target.GeometryUri);
        _ = restored.Target.SlotId.Should().Be(target.SlotId);
        _ = restored.Target.LayoutRevision.Should().Be(target.LayoutRevision);
        _ = restored.Material.Should().Be(new Uri("asset:///Content/Materials/Iron.omat.json"));
        _ = restored.Target.Should().Be(target);
    }

    [TestMethod]
    public async Task GeometryMaterialSlot_SerializesStableTargetAndMaterialIdentityOnly()
    {
        var scene = new Scene { Name = "Material Slot Scene" };
        var node = new SceneNode(scene) { Name = "Cube" };
        var geometry = new GeometryComponent
        {
            Name = "Geometry",
            Node = node,
            Geometry = new Uri("asset:///Engine/Generated/BasicShapes/Cube"),
        };
        var target = new MaterialSlotTarget(geometry.Geometry, Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));
        var materialSlot = new MaterialsSlot { Target = target, Material = new Uri("asset:///Content/Materials/Red.omat.json") };
        geometry.OverrideSlots.Add(materialSlot);
        node.Components.Add(geometry);
        scene.RootNodes.Add(node);

        var stream = new MemoryStream();
        await using var streamLifetime = stream.ConfigureAwait(false);
        await SceneSerializer.SerializeAsync(stream, scene).ConfigureAwait(false);
        var json = Encoding.UTF8.GetString(stream.ToArray());

        _ = json.Should().Contain("asset:///Content/Materials/Red.omat.json");
        _ = json.Should().NotContain("DescriptorPath");
        _ = json.Should().NotContain("CookedPath");
        _ = json.Should().NotContain("PrimaryState");
        _ = json.Should().NotContain("DerivedState");
        _ = json.Should().NotContain("RuntimeAvailability");
        _ = json.Should().NotContain("ThumbnailKey");
        _ = json.Should().NotContain("Diagnostic");
        stream.Position = 0;
        var restored = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);
        _ = restored.RootNodes.Single().Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().Single().Target.Should().Be(target);
    }

    [TestMethod]
    public void Material_SetToANonAssetUri_ThrowsAndKeepsTheMaterial()
    {
        var iron = new Uri("asset:///Content/Materials/Iron.omat.json");
        var slot = new MaterialsSlot { Target = GateTarget(), Material = iron };

        var act = () => slot.Material = new Uri("https://example.com/Content/Materials/Iron.omat.json");

        _ = act.Should().Throw<ArgumentException>();
        _ = slot.Material.Should().Be(iron);
    }

    [TestMethod]
    public void MaterialSlotTarget_NonAssetGeometry_Throws()
    {
        var act = () => new MaterialSlotTarget(new Uri("file:///C:/Content/Meshes/Gate.geometry"), Guid.NewGuid(), "rev-gate-42");

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void MaterialSlotTarget_WithNonAssetGeometry_Throws()
    {
        var target = GateTarget();

        var act = () => target with { GeometryUri = new Uri("https://example.com/Content/Meshes/Gate.geometry") };

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [DataRow("asset:///Content/Meshes/Gate.geometry", "https://example.com/Content/Materials/Iron.omat.json")]
    [DataRow("https://example.com/Content/Meshes/Gate.geometry", "asset:///Content/Materials/Iron.omat.json")]
    public void CreateAndHydrate_UriNotAnAsset_ThrowsJsonException(string geometry, string material)
    {
        var data = new MaterialsSlotData
        {
            GeometryUri = new Uri(geometry),
            SlotId = Guid.Parse("40000000-0000-0000-0000-000000000001"),
            LayoutRevision = "rev-gate-42",
            MaterialUri = new Uri(material),
        };

        var act = () => OverrideSlot.CreateAndHydrate(data);

        _ = act.Should().Throw<JsonException>();
    }

    private static MaterialSlotTarget GateTarget()
        => new(new Uri("asset:///Content/Meshes/Gate.geometry"), Guid.Parse("40000000-0000-0000-0000-000000000001"), "rev-gate-42");
}

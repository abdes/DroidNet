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
using Oxygen.Managed.Core;

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
            AssetPath.Parse("/Content/Meshes/Gate.geometry"),
            Guid.Parse("40000000-0000-0000-0000-000000000001"),
            "rev-gate-42");

        var slot = new MaterialsSlot
        {
            Target = target,
            Material = AssetPath.Parse("/Content/Materials/Iron.omat.json"),
        };

        var dto = (MaterialsSlotData)slot.Dehydrate();
        var restored = (MaterialsSlot)OverrideSlot.CreateAndHydrate(dto);

        _ = restored.Target.Geometry.Should().Be(target.Geometry);
        _ = restored.Target.SlotId.Should().Be(target.SlotId);
        _ = restored.Target.LayoutRevision.Should().Be(target.LayoutRevision);
        _ = restored.Material.Should().Be(AssetPath.Parse("/Content/Materials/Iron.omat.json"));
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
            Geometry = AssetPath.Parse("/Engine/Generated/BasicShapes/Cube"),
        };
        var target = new MaterialSlotTarget(geometry.Geometry, Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));
        var materialSlot = new MaterialsSlot { Target = target, Material = AssetPath.Parse("/Content/Materials/Red.omat.json") };
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
    public void Material_SetToNull_ThrowsAndKeepsTheMaterial()
    {
        var iron = AssetPath.Parse("/Content/Materials/Iron.omat");
        var slot = new MaterialsSlot { Target = GateTarget(), Material = iron };

        var act = () => slot.Material = null!;

        _ = act.Should().Throw<ArgumentNullException>();
        _ = slot.Material.Should().Be(iron);
    }

    [TestMethod]
    [DataRow(false, true)]
    [DataRow(true, false)]
    public void CreateAndHydrate_MissingReference_ThrowsJsonException(bool hasGeometry, bool hasMaterial)
    {
        var data = new MaterialsSlotData
        {
            GeometryUri = hasGeometry ? AssetPath.Parse("/Content/Meshes/Gate.geometry") : null!,
            SlotId = Guid.Parse("40000000-0000-0000-0000-000000000001"),
            LayoutRevision = "rev-gate-42",
            MaterialUri = hasMaterial ? AssetPath.Parse("/Content/Materials/Iron.omat") : null!,
        };

        var act = () => OverrideSlot.CreateAndHydrate(data);

        _ = act.Should().Throw<JsonException>();
    }

    private static MaterialSlotTarget GateTarget()
        => new(AssetPath.Parse("/Content/Meshes/Gate.geometry"), Guid.Parse("40000000-0000-0000-0000-000000000001"), "rev-gate-42");
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public class SlotSerializationTests
{
    [TestMethod]
    public void OverridableProperty_FromDto_ToDto_RoundTrip()
    {
        var def = true;
        var p = OverridableProperty.FromDefault(def);

        // not overridden -> ToDto is null
        _ = p.ToDto().Should().BeNull();

        var p2 = p.WithOverride(false);
        var dto = p2.ToDto();

        _ = dto.Should().NotBeNull();
        _ = dto!.IsOverridden.Should().BeTrue();
        _ = dto.OverrideValue.Should().BeFalse();

        var recovered = OverridableProperty.FromDto(def, dto);
        _ = recovered.IsOverridden.Should().BeTrue();
        _ = recovered.OverrideValue.Should().BeFalse();
    }

    [TestMethod]
    public void RenderingSlot_RoundTrips()
    {
        var s = new RenderingSlot();
        s.IsVisible = s.IsVisible.WithOverride(false);

        var dto = s.Dehydrate();
        _ = dto.Should().BeOfType<RenderingSlotData>();

        var recreated = OverrideSlot.CreateAndHydrate(dto) as RenderingSlot;
        _ = recreated.Should().NotBeNull();
        _ = recreated!.IsVisible.IsOverridden.Should().BeTrue();
        _ = recreated.IsVisible.OverrideValue.Should().BeFalse();
    }

    [TestMethod]
    public void LightingSlot_RoundTrips()
    {
        var s = new LightingSlot();
        s.CastShadows = s.CastShadows.WithOverride(false);
        s.ReceiveShadows = s.ReceiveShadows.WithOverride(true);

        var dto = s.Dehydrate();
        _ = dto.Should().BeOfType<LightingSlotData>();

        var recreated = OverrideSlot.CreateAndHydrate(dto) as LightingSlot;
        _ = recreated.Should().NotBeNull();
        _ = recreated!.CastShadows.IsOverridden.Should().BeTrue();
        _ = recreated.CastShadows.OverrideValue.Should().BeFalse();
        _ = recreated.ReceiveShadows.IsOverridden.Should().BeTrue();
        _ = recreated.ReceiveShadows.OverrideValue.Should().BeTrue();
    }

    [TestMethod]
    public void LevelOfDetailSlot_RoundTrips()
    {
        var s = new LevelOfDetailSlot();
        var defaultPolicy = s.LodPolicy.DefaultValue; // FixedLodPolicy
        var overridePolicy = new Oxygen.Editor.World.Policies.FixedLodPolicy { LodIndex = 2 };
        s.LodPolicy = s.LodPolicy.WithOverride(overridePolicy);

        var dto = s.Dehydrate();
        _ = dto.Should().BeOfType<LevelOfDetailSlotData>();

        var recreated = OverrideSlot.CreateAndHydrate(dto) as LevelOfDetailSlot;
        _ = recreated.Should().NotBeNull();
        _ = recreated!.LodPolicy.IsOverridden.Should().BeTrue();
        _ = recreated.LodPolicy.OverrideValue.Should().BeEquivalentTo(overridePolicy);
    }
}

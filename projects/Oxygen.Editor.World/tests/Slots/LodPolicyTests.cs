// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Tests.Slots;

/// <summary>
/// Verifies polymorphic serialization and deserialization of <see cref="LodPolicy"/> subtypes with their type discriminators.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(LodPolicy))]
public sealed class LodPolicyTests
{
    [TestMethod]
    public void FixedLodPolicy_SerializesWithDiscriminatorAndDeserializes()
    {
        var policy = new FixedLodPolicy { LodIndex = 2 };
        var slotData = new LevelOfDetailSlotData
        {
            LodPolicy = new OverridablePropertyData<LodPolicy>
            {
                IsOverridden = true,
                OverrideValue = policy,
            },
        };

        var json = JsonSerializer.Serialize(slotData, SceneJsonContext.Default.LevelOfDetailSlotData);
        _ = json.Should().Contain("\"$type\": \"Fixed\"");
        _ = json.Should().Contain("\"LodIndex\": 2");

        var restored = JsonSerializer.Deserialize(json, SceneJsonContext.Default.LevelOfDetailSlotData);
        _ = restored.Should().NotBeNull();
        _ = restored!.LodPolicy.Should().NotBeNull();
        _ = restored.LodPolicy!.IsOverridden.Should().BeTrue();
        _ = restored.LodPolicy.OverrideValue.Should().BeOfType<FixedLodPolicy>();
        var restoredFixed = (FixedLodPolicy)restored.LodPolicy.OverrideValue!;
        _ = restoredFixed.LodIndex.Should().Be(2);
    }

    [TestMethod]
    public void DistanceLodPolicy_SerializesWithDiscriminatorAndDeserializes()
    {
        var policy = new DistanceLodPolicy { Distances = [10f, 25f, 50f] };
        var slotData = new LevelOfDetailSlotData
        {
            LodPolicy = new OverridablePropertyData<LodPolicy>
            {
                IsOverridden = true,
                OverrideValue = policy,
            },
        };

        var json = JsonSerializer.Serialize(slotData, SceneJsonContext.Default.LevelOfDetailSlotData);
        _ = json.Should().Contain("\"$type\": \"Distance\"");

        var restored = JsonSerializer.Deserialize(json, SceneJsonContext.Default.LevelOfDetailSlotData);
        _ = restored.Should().NotBeNull();
        _ = restored!.LodPolicy.Should().NotBeNull();
        _ = restored.LodPolicy!.OverrideValue.Should().BeOfType<DistanceLodPolicy>();
        var restoredDistance = (DistanceLodPolicy)restored.LodPolicy.OverrideValue!;
        _ = restoredDistance.Distances.Should().Equal([10f, 25f, 50f]);
    }

    [TestMethod]
    public void ScreenSpaceErrorLodPolicy_SerializesWithDiscriminatorAndDeserializes()
    {
        var policy = new ScreenSpaceErrorLodPolicy { Thresholds = [2f, 5f, 12f] };
        var slotData = new LevelOfDetailSlotData
        {
            LodPolicy = new OverridablePropertyData<LodPolicy>
            {
                IsOverridden = true,
                OverrideValue = policy,
            },
        };

        var json = JsonSerializer.Serialize(slotData, SceneJsonContext.Default.LevelOfDetailSlotData);
        _ = json.Should().Contain("\"$type\": \"ScreenSpaceError\"");

        var restored = JsonSerializer.Deserialize(json, SceneJsonContext.Default.LevelOfDetailSlotData);
        _ = restored.Should().NotBeNull();
        _ = restored!.LodPolicy.Should().NotBeNull();
        _ = restored.LodPolicy!.OverrideValue.Should().BeOfType<ScreenSpaceErrorLodPolicy>();
        var restoredSse = (ScreenSpaceErrorLodPolicy)restored.LodPolicy.OverrideValue!;
        _ = restoredSse.Thresholds.Should().Equal([2f, 5f, 12f]);
    }
}

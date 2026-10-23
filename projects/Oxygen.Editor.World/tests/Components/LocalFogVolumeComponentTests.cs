// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies local fog volume component hydration and serialization round trips.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(LocalFogVolumeComponent))]
public sealed class LocalFogVolumeComponentTests
{
    [TestMethod]
    public void LocalFogVolume_RoundTripsThroughItsData()
    {
        var volume = new LocalFogVolumeComponent
        {
            Name = "Fog",
            Enabled = false,
            RadialFogExtinction = 2f,
            HeightFogExtinction = 0.5f,
            HeightFogFalloff = 10f,
            HeightFogOffset = -1f,
            FogPhaseG = 0.6f,
            FogAlbedo = new Vector3(0.5f, 0.6f, 0.7f),
            FogEmissive = new Vector3(0.1f, 0.2f, 0.3f),
            SortPriority = -4,
        };

        var json = JsonSerializer.Serialize<ComponentData>(volume.Dehydrate(), SceneJsonContext.Default.ComponentData);
        var restored = (LocalFogVolumeComponent)GameComponent.CreateAndHydrate(JsonSerializer.Deserialize(json, SceneJsonContext.Default.ComponentData)!);

        _ = restored.Dehydrate().Should().Be(volume.Dehydrate());
    }

    [TestMethod]
    public void LocalFogVolume_SettersOutsideTheSchemaRange_ThrowWithoutMutation()
    {
        var volume = new LocalFogVolumeComponent { Name = "Fog" };
        var before = volume.Dehydrate();
        Action[] changes =
        [
            () => volume.RadialFogExtinction = -1f,
            () => volume.HeightFogExtinction = float.NaN,
            () => volume.HeightFogFalloff = -1f,
            () => volume.HeightFogOffset = float.PositiveInfinity,
            () => volume.FogPhaseG = 1f,
            () => volume.FogAlbedo = new Vector3(0.5f, 1.5f, 0.5f),
            () => volume.FogEmissive = new Vector3(-1, 0, 0),
            () => volume.SortPriority = 128,
        ];

        foreach (var change in changes)
        {
            _ = change.Should().Throw<ArgumentOutOfRangeException>();
        }

        _ = volume.Dehydrate().Should().Be(before);
    }

    [TestMethod]
    public void LocalFogVolume_HydrateOutsideTheSchemaRange_ThrowsWithoutMutation()
    {
        var volume = new LocalFogVolumeComponent { Name = "Accepted" };

        Action hydrate = () => volume.Hydrate(new LocalFogVolumeData { Name = "Rejected", SortPriority = -128 });

        _ = hydrate.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be(-128);
        _ = volume.Name.Should().Be("Accepted");
        _ = volume.SortPriority.Should().Be(SceneSchema.LocalFogVolume.Defaults.SortPriority);
    }
}

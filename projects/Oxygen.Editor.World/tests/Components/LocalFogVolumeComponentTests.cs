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
}

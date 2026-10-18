// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies point light component defaults and DTO hydration/dehydration round trips.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(PointLightComponent))]
public sealed class PointLightComponentTests
{
    [TestMethod]
    public void PointLight_Defaults_ShouldHaveExpectedValues()
    {
        var light = new PointLightComponent { Name = "Point" };

        _ = light.LuminousFluxLumens.Should().Be(800f);
        _ = light.Range.Should().Be(10f);
        _ = light.SourceRadius.Should().Be(0f);
        _ = light.Color.Should().Be(Vector3.One);
        _ = light.AffectsWorld.Should().BeTrue();
    }

    [TestMethod]
    public void PointLight_Hydrate_Dehydrate_RoundTrip()
    {
        var source = new PointLightData
        {
            Name = "Hallway Light",
            LuminousFluxLumens = 1400f,
            Range = 12f,
            SourceRadius = 0.25f,
            Color = new Vector3(1f, 0.9f, 0.8f),
            AffectsWorld = true,
            CastsShadows = true,
            ExposureCompensation = 0.5f,
        };

        var light = (PointLightComponent)GameComponent.CreateAndHydrate(source);
        var dehydrated = (PointLightData)light.Dehydrate();

        _ = light.Name.Should().Be("Hallway Light");
        _ = light.LuminousFluxLumens.Should().Be(1400f);
        _ = light.Range.Should().Be(12f);
        _ = light.SourceRadius.Should().Be(0.25f);
        _ = light.Color.Should().Be(new Vector3(1f, 0.9f, 0.8f));
        _ = dehydrated.Should().BeEquivalentTo(source);
    }
}

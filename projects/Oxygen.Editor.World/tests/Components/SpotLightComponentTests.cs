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
/// Verifies spot light component defaults and DTO hydration/dehydration round trips.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SpotLightComponent))]
public sealed class SpotLightComponentTests
{
    [TestMethod]
    public void SpotLight_Defaults_ShouldHaveExpectedValues()
    {
        var light = new SpotLightComponent { Name = "Spot" };

        _ = light.LuminousFluxLumens.Should().Be(800f);
        _ = light.Range.Should().Be(10f);
        _ = light.SourceRadius.Should().Be(0f);
        _ = light.InnerConeAngleRadians.Should().Be(0.4f);
        _ = light.OuterConeAngleRadians.Should().Be(0.6f);
        _ = light.Color.Should().Be(Vector3.One);
        _ = light.AffectsWorld.Should().BeTrue();
    }

    [TestMethod]
    public void SpotLight_Hydrate_Dehydrate_RoundTrip()
    {
        var source = new SpotLightData
        {
            Name = "Stage Spotlight",
            LuminousFluxLumens = 2500f,
            Range = 30f,
            SourceRadius = 0.15f,
            InnerConeAngleRadians = 0.35f,
            OuterConeAngleRadians = 0.55f,
            Color = new Vector3(0.9f, 0.95f, 1f),
            AffectsWorld = true,
            CastsShadows = true,
            ExposureCompensation = -0.5f,
        };

        var light = (SpotLightComponent)GameComponent.CreateAndHydrate(source);
        var dehydrated = (SpotLightData)light.Dehydrate();

        _ = light.Name.Should().Be("Stage Spotlight");
        _ = light.LuminousFluxLumens.Should().Be(2500f);
        _ = light.Range.Should().Be(30f);
        _ = light.InnerConeAngleRadians.Should().Be(0.35f);
        _ = light.OuterConeAngleRadians.Should().Be(0.55f);
        _ = dehydrated.Should().BeEquivalentTo(source);
    }
}

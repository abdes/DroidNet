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
    public void SpotLight_Defaults_ComeFromTheSceneSchema()
    {
        var light = new SpotLightComponent { Name = "Spot" };
        var data = new SpotLightData { Name = "Spot" };

        _ = light.LuminousFluxLumens.Should().Be(SceneSchema.SpotLight.Defaults.LuminousFluxLumens).And.Be(data.LuminousFluxLumens);
        _ = light.Range.Should().Be(SceneSchema.SpotLight.Defaults.Range).And.Be(data.Range);
        _ = light.SourceRadius.Should().Be(SceneSchema.SpotLight.Defaults.SourceRadius).And.Be(data.SourceRadius);
        _ = light.InnerConeAngleRadians.Should().Be(SceneSchema.SpotLight.Defaults.InnerConeAngleRadians).And.Be(data.InnerConeAngleRadians);
        _ = light.OuterConeAngleRadians.Should().Be(SceneSchema.SpotLight.Defaults.OuterConeAngleRadians).And.Be(data.OuterConeAngleRadians);
        _ = light.Color.Should().Be(SceneSchema.SpotLight.Defaults.Color).And.Be(data.Color);
        _ = light.AffectsWorld.Should().Be(SceneSchema.SpotLight.Defaults.AffectsWorld).And.Be(data.AffectsWorld);
        _ = light.CastsShadows.Should().Be(SceneSchema.SpotLight.Defaults.CastsShadows).And.Be(data.CastsShadows);
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

    [TestMethod]
    public void SpotLight_SettersOutsideTheSchemaRange_ThrowWithoutMutation()
    {
        var light = new SpotLightComponent { Name = "Spot" };
        var before = light.Dehydrate();
        Action[] changes =
        [
            () => light.LuminousFluxLumens = float.NegativeInfinity,
            () => light.Range = 0f,
            () => light.SourceRadius = -0.1f,
            () => light.InnerConeAngleRadians = -0.1f,
            () => light.OuterConeAngleRadians = 2f,
        ];

        foreach (var change in changes)
        {
            _ = change.Should().Throw<ArgumentOutOfRangeException>();
        }

        _ = light.Dehydrate().Should().BeEquivalentTo(before);
    }

    [TestMethod]
    public void SpotLight_HydrateOutsideTheSchemaRange_ThrowsWithoutMutation()
    {
        var light = new SpotLightComponent { Name = "Accepted" };

        Action hydrate = () => light.Hydrate(new SpotLightData { Name = "Rejected", OuterConeAngleRadians = 2f });

        _ = hydrate.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be(2f);
        _ = light.Name.Should().Be("Accepted");
        _ = light.OuterConeAngleRadians.Should().Be(SceneSchema.SpotLight.Defaults.OuterConeAngleRadians);
    }
}

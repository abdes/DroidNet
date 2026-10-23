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
    public void PointLight_Defaults_ComeFromTheSceneSchema()
    {
        var light = new PointLightComponent { Name = "Point" };
        var data = new PointLightData { Name = "Point" };

        _ = light.LuminousFluxLumens.Should().Be(SceneSchema.PointLight.Defaults.LuminousFluxLumens).And.Be(data.LuminousFluxLumens);
        _ = light.Range.Should().Be(SceneSchema.PointLight.Defaults.Range).And.Be(data.Range);
        _ = light.SourceRadius.Should().Be(SceneSchema.PointLight.Defaults.SourceRadius).And.Be(data.SourceRadius);
        _ = light.Color.Should().Be(SceneSchema.PointLight.Defaults.Color).And.Be(data.Color);
        _ = light.AffectsWorld.Should().Be(SceneSchema.PointLight.Defaults.AffectsWorld).And.Be(data.AffectsWorld);
        _ = light.CastsShadows.Should().Be(SceneSchema.PointLight.Defaults.CastsShadows).And.Be(data.CastsShadows);
        _ = light.ExposureCompensation.Should().Be(SceneSchema.PointLight.Defaults.ExposureCompensation).And.Be(data.ExposureCompensation);
        _ = light.ShadowBias.Should().Be(SceneSchema.PointLight.Defaults.ShadowBias).And.Be(data.Shadow!.Bias);
        _ = light.ShadowNormalBias.Should().Be(SceneSchema.PointLight.Defaults.ShadowNormalBias).And.Be(data.Shadow!.NormalBias);
        _ = light.ContactShadows.Should().Be(SceneSchema.PointLight.Defaults.ContactShadows).And.Be(data.Shadow!.ContactShadows);
        _ = light.ShadowResolutionHint.Should().Be(SceneSchema.PointLight.Defaults.ShadowResolutionHint).And.Be(data.Shadow!.ResolutionHint);
    }

    [TestMethod]
    public void PointLight_HydratedWithoutShadowSettings_TakesTheSchemaShadowDefaults()
    {
        var light = (PointLightComponent)GameComponent.CreateAndHydrate(new PointLightData { Name = "Point", Shadow = null });

        _ = light.ShadowNormalBias.Should().Be(SceneSchema.PointLight.Defaults.ShadowNormalBias);
        _ = light.ShadowResolutionHint.Should().Be(SceneSchema.PointLight.Defaults.ShadowResolutionHint);
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

    [TestMethod]
    public void PointLight_SettersOutsideTheSchemaRange_ThrowWithoutMutation()
    {
        var light = new PointLightComponent { Name = "Point" };
        var before = light.Dehydrate();
        Action[] changes =
        [
            () => light.LuminousFluxLumens = 0f,
            () => light.Range = -1f,
            () => light.SourceRadius = float.NaN,
            () => light.Color = new Vector3(0, -0.5f, 0),
            () => light.ExposureCompensation = 11f,
        ];

        foreach (var change in changes)
        {
            _ = change.Should().Throw<ArgumentOutOfRangeException>();
        }

        _ = light.Dehydrate().Should().BeEquivalentTo(before);
    }

    [TestMethod]
    public void PointLight_HydrateOutsideTheSchemaRange_ThrowsWithoutMutation()
    {
        var light = new PointLightComponent { Name = "Accepted" };

        Action hydrate = () => light.Hydrate(new PointLightData { Name = "Rejected", SourceRadius = -1f });

        _ = hydrate.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be(-1f);
        _ = light.Name.Should().Be("Accepted");
        _ = light.SourceRadius.Should().Be(SceneSchema.PointLight.Defaults.SourceRadius);
    }
}

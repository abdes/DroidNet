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
/// Verifies directional light component defaults, property notifications, and serialization round trips.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(DirectionalLightComponent))]
public sealed class LightComponentTests
{
    [TestMethod]
    public void DirectionalLight_Defaults_ComeFromTheSceneSchema()
    {
        var cascades = new Vector4(SceneSchema.DirectionalLight.Defaults.CascadeDistance0, SceneSchema.DirectionalLight.Defaults.CascadeDistance1, SceneSchema.DirectionalLight.Defaults.CascadeDistance2, SceneSchema.DirectionalLight.Defaults.CascadeDistance3);
        var light = new DirectionalLightComponent { Name = "Sun" };
        var data = new DirectionalLightData { Name = "Sun" };

        _ = light.AffectsWorld.Should().Be(SceneSchema.DirectionalLight.Defaults.AffectsWorld).And.Be(data.AffectsWorld);
        _ = light.Color.Should().Be(SceneSchema.DirectionalLight.Defaults.Color).And.Be(data.Color);
        _ = light.CastsShadows.Should().Be(SceneSchema.DirectionalLight.Defaults.CastsShadows).And.Be(data.CastsShadows);
        _ = light.IntensityLux.Should().Be(SceneSchema.DirectionalLight.Defaults.IntensityLux).And.Be(data.IntensityLux);
        _ = light.AngularSizeRadians.Should().Be(SceneSchema.DirectionalLight.Defaults.AngularSizeRadians).And.Be(data.AngularSizeRadians);
        _ = light.ExposureCompensation.Should().Be(SceneSchema.DirectionalLight.Defaults.ExposureCompensation).And.Be(data.ExposureCompensation);
        _ = light.UsePerPixelAtmosphereTransmittance.Should().Be(SceneSchema.DirectionalLight.Defaults.UsePerPixelAtmosphereTransmittance).And.Be(data.UsePerPixelAtmosphereTransmittance);
        _ = light.AtmosphereSlot.Should().Be(SceneSchema.DirectionalLight.Defaults.AtmosphereSlot).And.Be(data.AtmosphereSlot);
        _ = light.AtmosphereDiskLuminanceScaleRgb.Should().Be(SceneSchema.DirectionalLight.Defaults.AtmosphereDiskLuminanceScaleRgb).And.Be(data.AtmosphereDiskLuminanceScaleRgb);
        _ = light.ShadowBias.Should().Be(SceneSchema.DirectionalLight.Defaults.ShadowBias).And.Be(data.Shadow!.Bias);
        _ = light.ShadowNormalBias.Should().Be(SceneSchema.DirectionalLight.Defaults.ShadowNormalBias).And.Be(data.Shadow!.NormalBias);
        _ = light.ContactShadows.Should().Be(SceneSchema.DirectionalLight.Defaults.ContactShadows).And.Be(data.Shadow!.ContactShadows);
        _ = light.ShadowResolutionHint.Should().Be(SceneSchema.DirectionalLight.Defaults.ShadowResolutionHint).And.Be(data.Shadow!.ResolutionHint);
        _ = light.CascadeCount.Should().Be(SceneSchema.DirectionalLight.Defaults.CascadeCount).And.Be(data.CascadeCount);
        _ = light.SplitMode.Should().Be(SceneSchema.DirectionalLight.Defaults.SplitMode).And.Be(data.SplitMode);
        _ = light.MaxShadowDistance.Should().Be(SceneSchema.DirectionalLight.Defaults.MaxShadowDistance).And.Be(data.MaxShadowDistance);
        _ = light.CascadeDistances.Should().Be(cascades).And.Be(data.CascadeDistances);
        _ = light.DistributionExponent.Should().Be(SceneSchema.DirectionalLight.Defaults.DistributionExponent).And.Be(data.DistributionExponent);
        _ = light.TransitionFraction.Should().Be(SceneSchema.DirectionalLight.Defaults.TransitionFraction).And.Be(data.TransitionFraction);
        _ = light.DistanceFadeoutFraction.Should().Be(SceneSchema.DirectionalLight.Defaults.DistanceFadeoutFraction).And.Be(data.DistanceFadeoutFraction);
    }

    [TestMethod]
    public void DirectionalLightData_Defaults_ShouldHydrateUnassignedDirectionalLight()
    {
        var light = GameComponent.CreateAndHydrate(new DirectionalLightData { Name = "Sun" }) as DirectionalLightComponent;

        _ = light.Should().NotBeNull();
        _ = light!.IntensityLux.Should().Be(SceneSchema.DirectionalLight.Defaults.IntensityLux);
        _ = light.AngularSizeRadians.Should().Be(SceneSchema.DirectionalLight.Defaults.AngularSizeRadians);
        _ = light.ExposureCompensation.Should().Be(SceneSchema.DirectionalLight.Defaults.ExposureCompensation);
        _ = light.UsePerPixelAtmosphereTransmittance.Should().Be(SceneSchema.DirectionalLight.Defaults.UsePerPixelAtmosphereTransmittance);
        _ = light.AtmosphereSlot.Should().Be(SceneSchema.DirectionalLight.Defaults.AtmosphereSlot);
        _ = light.CascadeCount.Should().Be(SceneSchema.DirectionalLight.Defaults.CascadeCount);
        _ = light.CastsShadows.Should().Be(SceneSchema.DirectionalLight.Defaults.CastsShadows);
    }

    [TestMethod]
    public void DirectionalLight_Setters_ShouldRaisePropertyChangedForEveryEditableField()
    {
        var light = new DirectionalLightComponent { Name = "Sun" };
        var changed = new List<string?>();
        light.PropertyChanged += (_, args) => changed.Add(args.PropertyName);

        light.Color = new Vector3(0.5f, 0.25f, 0.125f);
        light.AtmosphereDiskLuminanceScaleRgb = new Vector3(1.2f, 0.8f, 0.5f);
        light.IntensityLux = 42_000f;
        light.AtmosphereSlot = AtmosphereLightSlot.Secondary;
        light.UsePerPixelAtmosphereTransmittance = true;
        light.CastsShadows = !SceneSchema.DirectionalLight.Defaults.CastsShadows;
        light.ShadowBias = 0.001f;
        light.ShadowNormalBias = 0.04f;
        light.ContactShadows = true;
        light.ShadowResolutionHint = ShadowResolutionHint.High;
        light.AffectsWorld = false;
        light.AngularSizeRadians = 0.02f;
        light.ExposureCompensation = 1.5f;
        light.CascadeCount = 3;
        light.SplitMode = DirectionalCsmSplitMode.ManualDistances;
        light.MaxShadowDistance = 256f;
        light.CascadeDistances = new Vector4(16f, 48f, 128f, 256f);
        light.DistributionExponent = 2f;
        light.TransitionFraction = 0.2f;
        light.DistanceFadeoutFraction = 0.3f;

        _ = changed.Should().Contain([
            nameof(DirectionalLightComponent.Color),
            nameof(DirectionalLightComponent.AtmosphereDiskLuminanceScaleRgb),
            nameof(DirectionalLightComponent.IntensityLux),
            nameof(DirectionalLightComponent.AtmosphereSlot),
            nameof(DirectionalLightComponent.UsePerPixelAtmosphereTransmittance),
            nameof(DirectionalLightComponent.CastsShadows),
            nameof(DirectionalLightComponent.ShadowBias),
            nameof(DirectionalLightComponent.ShadowNormalBias),
            nameof(DirectionalLightComponent.ContactShadows),
            nameof(DirectionalLightComponent.ShadowResolutionHint),
            nameof(DirectionalLightComponent.AffectsWorld),
            nameof(DirectionalLightComponent.AngularSizeRadians),
            nameof(DirectionalLightComponent.ExposureCompensation),
            nameof(DirectionalLightComponent.CascadeCount),
            nameof(DirectionalLightComponent.SplitMode),
            nameof(DirectionalLightComponent.MaxShadowDistance),
            nameof(DirectionalLightComponent.CascadeDistances),
            nameof(DirectionalLightComponent.DistributionExponent),
            nameof(DirectionalLightComponent.TransitionFraction),
            nameof(DirectionalLightComponent.DistanceFadeoutFraction),
        ]);
    }

    [TestMethod]
    public void CompleteDirectionalValues_RoundTripAndInvalidHydrationIsAtomic()
    {
        var source = new DirectionalLightData
        {
            Name = "Secondary",
            AtmosphereSlot = AtmosphereLightSlot.Secondary,
            Color = new(0.4f, 0.7f, 1f),
            IntensityLux = 2345f,
            ExposureCompensation = 1.5f,
            UsePerPixelAtmosphereTransmittance = true,
            AtmosphereDiskLuminanceScaleRgb = new(2f, 1f, 0.5f),
            CastsShadows = true,
            Shadow = new()
            {
                Bias = 0.001f,
                NormalBias = 0.04f,
                ContactShadows = true,
                ResolutionHint = ShadowResolutionHint.High,
            },
            CascadeCount = 3,
            SplitMode = DirectionalCsmSplitMode.ManualDistances,
            CascadeDistances = new(5, 15, 40, 90),
            MaxShadowDistance = 90,
            DistributionExponent = 2,
            TransitionFraction = 0.2f,
            DistanceFadeoutFraction = 0.3f,
        };
        var light = new DirectionalLightComponent { Name = "Accepted" };
        light.Hydrate(source);
        _ = light.Dehydrate().Should().BeEquivalentTo(source);
        Action invalid = () => light.Hydrate(source with { Name = "Rejected", CascadeDistances = new(5, 2, 40, 90) });
        _ = invalid.Should().Throw<ArgumentException>();
        _ = light.Name.Should().Be("Secondary");
        _ = light.CascadeDistances.Should().Be(source.CascadeDistances);
    }

    [TestMethod]
    public void DirectionalLight_SettersOutsideTheSchemaRange_ThrowWithoutMutation()
    {
        var light = new DirectionalLightComponent { Name = "Sun" };
        var before = light.Dehydrate();
        Action[] changes =
        [
            () => light.Color = new Vector3(1.5f, 1, 1),
            () => light.ExposureCompensation = float.NaN,
            () => light.ShadowBias = -1f,
            () => light.ShadowNormalBias = -1f,
            () => light.ShadowResolutionHint = (ShadowResolutionHint)99,
            () => light.IntensityLux = -1f,
            () => light.AngularSizeRadians = 4f,
            () => light.AtmosphereSlot = (AtmosphereLightSlot)99,
            () => light.AtmosphereDiskLuminanceScaleRgb = new Vector3(-1, 1, 1),
            () => light.CascadeCount = 0,
            () => light.SplitMode = (DirectionalCsmSplitMode)99,
            () => light.MaxShadowDistance = 0f,
            () => light.CascadeDistances = new Vector4(8, 24, 64, -1),
            () => light.DistributionExponent = 0.5f,
            () => light.TransitionFraction = 2f,
            () => light.DistanceFadeoutFraction = float.PositiveInfinity,
        ];

        foreach (var change in changes)
        {
            _ = change.Should().Throw<ArgumentOutOfRangeException>();
        }

        _ = light.Dehydrate().Should().BeEquivalentTo(before);
    }
}

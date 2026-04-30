// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public sealed class LightComponentTests
{
    [TestMethod]
    public void DirectionalLight_Defaults_ShouldBeUsableSunLight()
    {
        var light = new DirectionalLightComponent { Name = "Sun" };

        _ = light.AffectsWorld.Should().BeTrue();
        _ = light.Color.Should().Be(System.Numerics.Vector3.One);
        _ = light.Mobility.Should().Be(LightMobility.Realtime);
        _ = light.IntensityLux.Should().Be(100_000f);
        _ = light.AngularSizeRadians.Should().BeApproximately(0.00935f, 0.00001f);
        _ = light.ExposureCompensation.Should().Be(0f);
        _ = light.EnvironmentContribution.Should().BeTrue();
        _ = light.IsSunLight.Should().BeTrue();
        _ = light.ShadowNormalBias.Should().Be(0.02f);
        _ = light.ShadowResolutionHint.Should().Be(ShadowResolutionHint.Medium);
        _ = light.CascadeCount.Should().Be(4);
        _ = light.CascadeDistances.Should().Be(new Vector4(8f, 24f, 64f, 160f));
        _ = light.DistributionExponent.Should().Be(3f);
        _ = light.TransitionFraction.Should().Be(0.1f);
        _ = light.DistanceFadeoutFraction.Should().Be(0.1f);
    }

    [TestMethod]
    public void DirectionalLightData_Defaults_ShouldHydrateUsableSunLight()
    {
        var light = GameComponent.CreateAndHydrate(new DirectionalLightData { Name = "Sun" }) as DirectionalLightComponent;

        _ = light.Should().NotBeNull();
        _ = light!.IntensityLux.Should().Be(100_000f);
        _ = light.AngularSizeRadians.Should().BeApproximately(0.00935f, 0.00001f);
        _ = light.ExposureCompensation.Should().Be(0f);
        _ = light.EnvironmentContribution.Should().BeTrue();
        _ = light.IsSunLight.Should().BeTrue();
        _ = light.CascadeCount.Should().Be(4);
        _ = light.CascadeDistances.Should().Be(new Vector4(8f, 24f, 64f, 160f));
    }

    [TestMethod]
    public void DirectionalLight_Setters_ShouldRaisePropertyChangedForEveryEditableField()
    {
        var light = new DirectionalLightComponent { Name = "Sun" };
        var changed = new List<string?>();
        light.PropertyChanged += (_, args) => changed.Add(args.PropertyName);

        light.Color = new Vector3(0.5f, 0.25f, 0.125f);
        light.Mobility = LightMobility.Mixed;
        light.IntensityLux = 42_000f;
        light.IsSunLight = false;
        light.EnvironmentContribution = false;
        light.CastsShadows = true;
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
            nameof(DirectionalLightComponent.Mobility),
            nameof(DirectionalLightComponent.IntensityLux),
            nameof(DirectionalLightComponent.IsSunLight),
            nameof(DirectionalLightComponent.EnvironmentContribution),
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
}

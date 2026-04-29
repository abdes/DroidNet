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
        _ = light.IntensityLux.Should().Be(100_000f);
        _ = light.AngularSizeRadians.Should().BeApproximately(0.00935f, 0.00001f);
        _ = light.ExposureCompensation.Should().Be(0f);
        _ = light.EnvironmentContribution.Should().BeTrue();
        _ = light.IsSunLight.Should().BeTrue();
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
    }

    [TestMethod]
    public void DirectionalLight_Setters_ShouldRaisePropertyChangedForEveryEditableField()
    {
        var light = new DirectionalLightComponent { Name = "Sun" };
        var changed = new List<string?>();
        light.PropertyChanged += (_, args) => changed.Add(args.PropertyName);

        light.Color = new Vector3(0.5f, 0.25f, 0.125f);
        light.IntensityLux = 42_000f;
        light.IsSunLight = false;
        light.EnvironmentContribution = false;
        light.CastsShadows = true;
        light.AffectsWorld = false;
        light.AngularSizeRadians = 0.02f;
        light.ExposureCompensation = 1.5f;

        _ = changed.Should().Contain([
            nameof(DirectionalLightComponent.Color),
            nameof(DirectionalLightComponent.IntensityLux),
            nameof(DirectionalLightComponent.IsSunLight),
            nameof(DirectionalLightComponent.EnvironmentContribution),
            nameof(DirectionalLightComponent.CastsShadows),
            nameof(DirectionalLightComponent.AffectsWorld),
            nameof(DirectionalLightComponent.AngularSizeRadians),
            nameof(DirectionalLightComponent.ExposureCompensation),
        ]);
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
        _ = light.EnvironmentContribution.Should().BeTrue();
        _ = light.IsSunLight.Should().BeTrue();
    }
}

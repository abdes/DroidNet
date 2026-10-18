// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests.Slots;

/// <summary>
/// Verifies <see cref="OverridableProperty{T}"/> override state transitions, effective values, and DTO round trips.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(OverridableProperty))]
public sealed class OverridablePropertyTests
{
    [TestMethod]
    public void EffectiveValue_WhenNotOverridden_ReturnsDefaultValue()
    {
        var property = OverridableProperty.FromDefault(100);

        _ = property.IsOverridden.Should().BeFalse();
        _ = property.DefaultValue.Should().Be(100);
        _ = property.EffectiveValue.Should().Be(100);
    }

    [TestMethod]
    public void WithOverride_AndClearOverride_ControlOverrideStateAndEffectiveValue()
    {
        var property = OverridableProperty.FromDefault(100);

        var overridden = property.WithOverride(250);
        _ = overridden.IsOverridden.Should().BeTrue();
        _ = overridden.DefaultValue.Should().Be(100);
        _ = overridden.OverrideValue.Should().Be(250);
        _ = overridden.EffectiveValue.Should().Be(250);

        var cleared = overridden.ClearOverride();
        _ = cleared.IsOverridden.Should().BeFalse();
        _ = cleared.DefaultValue.Should().Be(100);
        _ = cleared.EffectiveValue.Should().Be(100);
    }

    [TestMethod]
    public void ToDto_WhenNotOverridden_ReturnsNull()
    {
        var property = OverridableProperty.FromDefault("default");

        _ = property.ToDto().Should().BeNull();
    }

    [TestMethod]
    public void ToDto_WhenOverridden_ReturnsPopulatedDto()
    {
        var property = OverridableProperty.FromDefault("default").WithOverride("custom");

        var dto = property.ToDto();

        _ = dto.Should().NotBeNull();
        _ = dto!.IsOverridden.Should().BeTrue();
        _ = dto.OverrideValue.Should().Be("custom");
    }

    [TestMethod]
    public void FromDto_WhenIsOverriddenFalse_IgnoresStoredValue()
    {
        var dto = new OverridablePropertyData<int>
        {
            IsOverridden = false,
            OverrideValue = 999,
        };

        var property = OverridableProperty.FromDto(50, dto);

        _ = property.IsOverridden.Should().BeFalse();
        _ = property.DefaultValue.Should().Be(50);
        _ = property.EffectiveValue.Should().Be(50);
    }

    [TestMethod]
    public void FromDto_WhenIsOverriddenTrue_RestoresStoredValue()
    {
        var dto = new OverridablePropertyData<int>
        {
            IsOverridden = true,
            OverrideValue = 999,
        };

        var property = OverridableProperty.FromDto(50, dto);

        _ = property.IsOverridden.Should().BeTrue();
        _ = property.DefaultValue.Should().Be(50);
        _ = property.OverrideValue.Should().Be(999);
        _ = property.EffectiveValue.Should().Be(999);
    }
}

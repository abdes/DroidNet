// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Component-local field ids for <see cref="EngineComponentId.DirectionalLight"/>.
/// Mirrors <c>oxygen::interop::module::DirectionalLightField</c>.
/// </summary>
public enum DirectionalLightField
{
    /// <summary>Linear red color multiplier.</summary>
    ColorR = 0,

    /// <summary>Linear green color multiplier.</summary>
    ColorG = 1,

    /// <summary>Linear blue color multiplier.</summary>
    ColorB = 2,

    /// <summary>World-lighting participation flag.</summary>
    AffectsWorld = 3,

    /// <summary>Light mobility enum value.</summary>
    Mobility = 4,

    /// <summary>Shadow-casting flag.</summary>
    CastsShadows = 5,

    /// <summary>Shadow depth bias.</summary>
    ShadowBias = 6,

    /// <summary>Shadow normal bias.</summary>
    ShadowNormalBias = 7,

    /// <summary>Contact shadow flag.</summary>
    ContactShadows = 8,

    /// <summary>Shadow resolution hint enum value.</summary>
    ShadowResolutionHint = 9,

    /// <summary>Exposure compensation in EV.</summary>
    ExposureCompensation = 10,

    /// <summary>Directional light illuminance in lux.</summary>
    IntensityLux = 11,

    /// <summary>Directional light angular size in radians.</summary>
    AngularSizeRadians = 12,

    /// <summary>Environment contribution flag.</summary>
    EnvironmentContribution = 13,

    /// <summary>Primary sun candidate flag.</summary>
    IsSunLight = 14,

    /// <summary>Cascade count.</summary>
    CascadeCount = 15,

    /// <summary>Cascade split mode enum value.</summary>
    SplitMode = 16,

    /// <summary>Maximum shadow distance.</summary>
    MaxShadowDistance = 17,

    /// <summary>First cascade distance.</summary>
    CascadeDistance0 = 18,

    /// <summary>Second cascade distance.</summary>
    CascadeDistance1 = 19,

    /// <summary>Third cascade distance.</summary>
    CascadeDistance2 = 20,

    /// <summary>Fourth cascade distance.</summary>
    CascadeDistance3 = 21,

    /// <summary>Generated CSM distribution exponent.</summary>
    DistributionExponent = 22,

    /// <summary>Cascade transition fraction.</summary>
    TransitionFraction = 23,

    /// <summary>Shadow distance fadeout fraction.</summary>
    DistanceFadeoutFraction = 24,
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Component-local field ids shared by <see cref="EngineComponentId.PointLight"/> and
/// <see cref="EngineComponentId.SpotLight"/>; the common ids match <see cref="DirectionalLightField"/>.
/// Mirrors <c>oxygen::interop::module::LocalLightField</c>.
/// </summary>
public enum LocalLightField
{
    /// <summary>Linear red color multiplier.</summary>
    ColorR = 0,

    /// <summary>Linear green color multiplier.</summary>
    ColorG = 1,

    /// <summary>Linear blue color multiplier.</summary>
    ColorB = 2,

    /// <summary>Whether the light contributes to scene lighting.</summary>
    AffectsWorld = 3,

    /// <summary>Whether the light casts shadows.</summary>
    CastsShadows = 5,

    /// <summary>Shadow depth bias.</summary>
    ShadowBias = 6,

    /// <summary>Shadow normal bias.</summary>
    ShadowNormalBias = 7,

    /// <summary>Whether contact shadows are enabled.</summary>
    ContactShadows = 8,

    /// <summary>Shadow-map resolution hint.</summary>
    ShadowResolutionHint = 9,

    /// <summary>Light exposure compensation in EV.</summary>
    ExposureCompensation = 10,

    /// <summary>Luminous flux, in lumens.</summary>
    LuminousFluxLumens = 11,

    /// <summary>Influence range, in meters.</summary>
    Range = 12,

    /// <summary>Emitter source radius, in meters.</summary>
    SourceRadius = 13,

    /// <summary>Spot inner cone half-angle, in radians.</summary>
    InnerConeAngleRadians = 14,

    /// <summary>Spot outer cone half-angle, in radians.</summary>
    OuterConeAngleRadians = 15,
}

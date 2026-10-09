// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Component-local field ids of <see cref="EngineComponentId.LocalFogVolume"/>; they match the
/// native <c>LocalFogVolumeField</c> enum.
/// </summary>
public enum LocalFogVolumeField
{
    /// <summary>Volume enablement, 0 or 1.</summary>
    Enabled = 0,

    /// <summary>Extinction at the volume's center.</summary>
    RadialFogExtinction = 1,

    /// <summary>Extinction of the height-based density.</summary>
    HeightFogExtinction = 2,

    /// <summary>Height falloff.</summary>
    HeightFogFalloff = 3,

    /// <summary>Height offset relative to the volume's center.</summary>
    HeightFogOffset = 4,

    /// <summary>Phase anisotropy.</summary>
    FogPhaseG = 5,

    /// <summary>Albedo red channel.</summary>
    FogAlbedoR = 6,

    /// <summary>Albedo green channel.</summary>
    FogAlbedoG = 7,

    /// <summary>Albedo blue channel.</summary>
    FogAlbedoB = 8,

    /// <summary>Emissive red channel.</summary>
    FogEmissiveR = 9,

    /// <summary>Emissive green channel.</summary>
    FogEmissiveG = 10,

    /// <summary>Emissive blue channel.</summary>
    FogEmissiveB = 11,

    /// <summary>Composition order, an integer from -127 to 127.</summary>
    SortPriority = 12,
}

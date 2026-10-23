// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Components;

/// <summary>A rule that relates several values of one light, which no single property's range expresses.</summary>
public enum LightRule
{
    /// <summary>The active cascade distances of a directional light do not increase.</summary>
    CascadeDistancesNotIncreasing = 0,

    /// <summary>The range and the source radius of a local light together exceed what a float holds.</summary>
    ExtentNotRepresentable = 1,

    /// <summary>The inverse of a local light's range is not a normal float.</summary>
    InverseRangeNotRepresentable = 2,

    /// <summary>A spot light's inner cone is wider than its outer cone, its outer cone is closed, or both are a right angle.</summary>
    SpotConeInvalid = 3,

    /// <summary>The renderer cannot tell a spot light's inner and outer cone edges apart.</summary>
    SpotConeNotRepresentable = 4,

    /// <summary>The intensity the light's emission, color and exposure resolve to is not a normal float.</summary>
    IntensityNotRepresentable = 5,

    /// <summary>The radiance of a directional light's sun disk is not a normal float.</summary>
    DiskRadianceNotRepresentable = 6,
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>The authored mode in which a stored value applies.</summary>
public enum InspectorFieldApplicability
{
    /// <summary>The field is always presented.</summary>
    Always,

    /// <summary>The field applies to automatic exposure.</summary>
    AutoExposure,

    /// <summary>The field applies to automatic exposure with Spot metering.</summary>
    AutoExposureSpot,

    /// <summary>The field applies to manual exposure.</summary>
    ManualExposure,

    /// <summary>The existing presentation depends on the selected tone mapper.</summary>
    ToneMapping,

    /// <summary>The field applies while the atmosphere is the backdrop.</summary>
    AtmosphereBackdrop,

    /// <summary>The field applies while a cubemap is the backdrop.</summary>
    CubemapBackdrop,

    /// <summary>The field applies while a solid color is the backdrop.</summary>
    SolidColorBackdrop,

    /// <summary>The field scales the sky sphere: a cubemap backdrop, or a solid color that lights the scene.</summary>
    SkySphereTone,

    /// <summary>The field applies while the Sky Light takes its radiance from a cubemap.</summary>
    SkyLightCubemap,
}

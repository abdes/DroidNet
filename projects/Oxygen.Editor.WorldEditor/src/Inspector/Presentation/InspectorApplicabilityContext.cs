// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>The authored modes that decide which stored scene fields apply.</summary>
/// <param name="Exposure">The exposure mode.</param>
/// <param name="Metering">The auto exposure metering mode.</param>
/// <param name="ToneMapping">The tone mapper.</param>
/// <param name="Backdrop">The shown backdrop.</param>
/// <param name="SolidColorLightsScene">Whether a solid color backdrop also lights the scene.</param>
/// <param name="SkyLightSource">The Sky Light source.</param>
public readonly record struct InspectorApplicabilityContext(
    ExposureMode Exposure,
    MeteringMode Metering,
    ToneMappingMode ToneMapping,
    EnvironmentBackdrop Backdrop = EnvironmentBackdrop.Atmosphere,
    bool SolidColorLightsScene = true,
    SkyLightSource SkyLightSource = SkyLightSource.CapturedScene)
{
    /// <summary>Gets whether a field with the given applicability applies in these modes.</summary>
    /// <param name="applicability">The field's applicability.</param>
    /// <returns><see langword="true"/> when the stored value takes effect.</returns>
    public bool Applies(InspectorFieldApplicability applicability) => applicability switch
    {
        InspectorFieldApplicability.AutoExposure => this.Exposure == ExposureMode.Auto,
        InspectorFieldApplicability.AutoExposureSpot => this.Exposure == ExposureMode.Auto && this.Metering == MeteringMode.Spot,
        InspectorFieldApplicability.ManualExposure => this.Exposure == ExposureMode.Manual,
        InspectorFieldApplicability.ToneMapping => this.ToneMapping != ToneMappingMode.None,
        InspectorFieldApplicability.AtmosphereBackdrop => this.Backdrop == EnvironmentBackdrop.Atmosphere,
        InspectorFieldApplicability.CubemapBackdrop => this.Backdrop == EnvironmentBackdrop.Cubemap,
        InspectorFieldApplicability.SolidColorBackdrop => this.Backdrop == EnvironmentBackdrop.SolidColor,
        InspectorFieldApplicability.SkySphereTone => this.Backdrop == EnvironmentBackdrop.Cubemap
            || (this.Backdrop == EnvironmentBackdrop.SolidColor && this.SolidColorLightsScene),
        InspectorFieldApplicability.SkyLightCubemap => this.SkyLightSource == SkyLightSource.SpecifiedCubemap,
        _ => true,
    };

    /// <summary>Gets the note shown when a search reveals a stored value that does not apply.</summary>
    /// <param name="applicability">The field's applicability.</param>
    /// <returns>The note, or empty for fields that always apply.</returns>
    public string InapplicableNote(InspectorFieldApplicability applicability) => applicability switch
    {
        InspectorFieldApplicability.AutoExposureSpot => "Stored value; applies in Auto exposure mode with Spot metering.",
        InspectorFieldApplicability.AutoExposure => $"Stored value; applies in Auto exposure mode. Current mode: {this.Exposure}.",
        InspectorFieldApplicability.ManualExposure => $"Stored value; applies in Manual exposure mode. Current mode: {this.Exposure}.",
        InspectorFieldApplicability.ToneMapping => "Stored value; color grading is inactive while tone mapping is set to None.",
        InspectorFieldApplicability.AtmosphereBackdrop => "Stored value; applies while the backdrop is Atmosphere.",
        InspectorFieldApplicability.CubemapBackdrop => "Stored value; applies while the backdrop is Cubemap.",
        InspectorFieldApplicability.SolidColorBackdrop => "Stored value; applies while the backdrop is Solid color.",
        InspectorFieldApplicability.SkySphereTone => "Stored value; applies to a Cubemap backdrop, or a Solid color that lights the scene.",
        InspectorFieldApplicability.SkyLightCubemap => "Stored value; applies while the Sky Light source is Cubemap.",
        _ => string.Empty,
    };
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Collections.ObjectModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>Explicit presentation metadata for the 42 existing scene cards.</summary>
internal static class EnvironmentFieldCatalog
{
    /// <summary>Gets the ten nested source-field identities without consulting their views.</summary>
    internal static IReadOnlyList<AtmosphereSourceFieldIdentity> SourceFields { get; } = CreateSourceFields();

    /// <summary>Creates presentation for the explicitly inventoried scene fields.</summary>
    /// <returns>A new view-owned search model.</returns>
    internal static InspectorSearchModel Create()
        => new([
            Sky("AtmosphereEnabled", "/atmosphere_enabled", "Enabled"),
            Sky("SunDiskEnabled", "/sky_atmosphere/sun_disk_enabled", "Sun Disk"),
            Sky("PlanetRadiusKm", "/sky_atmosphere/planet_radius_meters", "Planet Radius", "PlanetGround", "planet_radius_meters km"),
            Sky("AtmosphereHeightKm", "/sky_atmosphere/atmosphere_height_meters", "Atmosphere Height", "PlanetGround", "atmosphere_height_meters km"),
            Sky("GroundAlbedo", "/sky_atmosphere/ground_albedo_rgb", "Ground Albedo", "PlanetGround", "ground_albedo_rgb Linear RGB"),
            Sky("RayleighScaleHeightKm", "/sky_atmosphere/rayleigh_scale_height_meters", "Rayleigh Height", "Scattering", "rayleigh_scale_height_meters km"),
            Sky("MieScaleHeightKm", "/sky_atmosphere/mie_scale_height_meters", "Mie Height", "Scattering", "mie_scale_height_meters km"),
            Sky("MieAnisotropy", "/sky_atmosphere/mie_anisotropy", "Mie Anisotropy", "Scattering"),
            Sky("SkyLuminance", "/sky_atmosphere/sky_luminance_factor_rgb", "Sky Luminance", aliases: "sky_luminance_factor_rgb RGB multiplier"),
            Sky("AerialPerspectiveDistanceScale", "/sky_atmosphere/aerial_perspective_distance_scale", "Distance scale", "AerialPerspective", "Aerial Distance"),
            Sky("AerialScatteringStrength", "/sky_atmosphere/aerial_scattering_strength", "Scattering strength", "AerialPerspective", "Aerial Strength"),
            Sky("AerialPerspectiveStartDepthMeters", "/sky_atmosphere/aerial_perspective_start_depth_meters", "Start distance", "AerialPerspective", "Aerial Start"),
            Sky("HeightFogContribution", "/sky_atmosphere/height_fog_contribution", "Height fog contribution", "AerialPerspective", "Height Fog"),
            new("Sources", SceneDocumentCommandService.DirectionalLight.AtmosphereSlot.Id, InspectorPropertyScope.Environment, "AtmosphereLights", group: null, "Sources", "Atmosphere Lights Primary and secondary directional light sources.", $"Sun Binding Sun Reference atmosphere_light_slot azimuth elevation direction angular diameter disk luminance transmittance primary secondary SunAzimuth SunElevation AngularSizeRadians DiskScale {string.Join(' ', SourceFields.SelectMany(static entry => entry.Properties).Select(static property => property.JsonPointer))}"),
            Exposure("ExposureEnabled", "exposure_enabled", "Enabled"),
            Exposure("ExposureMode", "exposure_mode", "Mode", aliases: "Manual ManualCamera Auto"),
            Exposure("ManualExposureEv", "manual_exposure_ev", "Manual EV", applicability: InspectorFieldApplicability.ManualExposure, aliases: "ManualExposure EV100"),
            Exposure("ExposureCompensation", "exposure_compensation_ev", "Compensation", aliases: "EV"),
            Exposure("ExposureKey", "exposure_key", "Key", "HistogramCalibration"),
            Exposure("AutoExposureMeteringMode", "auto_exposure_metering_mode", "Metering", "MeteringLimits", InspectorFieldApplicability.AutoExposure, "Average CenterWeighted Spot"),
            Exposure("AutoExposureMeteringMask", "auto_exposure_metering_mask", "Metering mask", "ExposureShaping", InspectorFieldApplicability.AutoExposure, "Choose an exposure metering mask"),
            Exposure("AutoExposureMinEv", "auto_exposure_min_ev", "Auto Min EV", "MeteringLimits", InspectorFieldApplicability.AutoExposure, "EV100"),
            Exposure("AutoExposureMaxEv", "auto_exposure_max_ev", "Auto Max EV", "MeteringLimits", InspectorFieldApplicability.AutoExposure, "EV100"),
            Exposure("AutoExposureSpeedUp", "auto_exposure_speed_up", "Adapt Up", "Adaptation", InspectorFieldApplicability.AutoExposure, "EV/s"),
            Exposure("AutoExposureSpeedDown", "auto_exposure_speed_down", "Adapt Down", "Adaptation", InspectorFieldApplicability.AutoExposure, "EV/s"),
            Exposure("AutoExposureTransitionDistanceEv", "auto_exposure_transition_distance_ev", "Adaptation transition distance", "Adaptation", InspectorFieldApplicability.AutoExposure, "EV"),
            Exposure("AutoExposureLowPercentile", "auto_exposure_low_percentile", "Low Percentile", "HistogramCalibration", InspectorFieldApplicability.AutoExposure),
            Exposure("AutoExposureHighPercentile", "auto_exposure_high_percentile", "High Percentile", "HistogramCalibration", InspectorFieldApplicability.AutoExposure),
            Exposure("AutoExposureMinLogLuminance", "auto_exposure_min_log_luminance", "Min Log Luminance", "HistogramCalibration", InspectorFieldApplicability.AutoExposure),
            Exposure("AutoExposureLogLuminanceRange", "auto_exposure_log_luminance_range", "Log Luminance Range", "HistogramCalibration", InspectorFieldApplicability.AutoExposure),
            Exposure("AutoExposureBlackInfluence", "auto_exposure_black_influence", "Dark-sample influence", "HistogramCalibration", InspectorFieldApplicability.AutoExposure),
            Exposure("AutoExposureTargetLuminance", "auto_exposure_target_luminance", "Target Luminance", "MeteringLimits", InspectorFieldApplicability.AutoExposure),
            Exposure("AutoExposureSpotMeterRadius", "auto_exposure_spot_meter_radius", "Spot Radius", "MeteringLimits", InspectorFieldApplicability.AutoExposureSpot),
            Exposure("AutoExposureCompensationCurve", "auto_exposure_compensation_curve", "Exposure-compensation curve", "ExposureShaping", InspectorFieldApplicability.AutoExposure, "Metered EV100 Compensation EV MeteredEv CompensationEv"),
            Post("ToneMapper", "tone_mapper", "Mapper", "ToneMapping", "Tone mapper selection and display transfer.", aliases: "AcesFitted Filmic Reinhard None"),
            Post("DisplayGamma", "display_gamma", "Display Gamma", "ToneMapping", "Tone mapper selection and display transfer."),
            Post("BloomIntensity", "bloom_intensity", "Intensity", "Bloom", "Bloom thresholding and contribution."),
            Post("BloomThreshold", "bloom_threshold", "Threshold", "Bloom", "Bloom thresholding and contribution."),
            Post("Saturation", "saturation", "Saturation", "ColorGrading", "Color and lens-style post-process adjustments.", InspectorFieldApplicability.ToneMapping),
            Post("Contrast", "contrast", "Contrast", "ColorGrading", "Color and lens-style post-process adjustments.", InspectorFieldApplicability.ToneMapping),
            Post("VignetteIntensity", "vignette_intensity", "Vignette", "ColorGrading", "Color and lens-style post-process adjustments.", InspectorFieldApplicability.ToneMapping),
            new("BackgroundColor", SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id, InspectorPropertyScope.Environment, "Background", group: null, "Color", "Background Fallback color when atmosphere rendering is disabled.", "BackgroundColor Linear RGB"),
        ]);

    private static ReadOnlyCollection<AtmosphereSourceFieldIdentity> CreateSourceFields()
    {
        var fields = new List<AtmosphereSourceFieldIdentity>(10);
        foreach (var role in new[] { AtmosphereLightSlot.Primary, AtmosphereLightSlot.Secondary })
        {
            ImmutableArray<PropertyId> rotation =
            [
                SceneDocumentCommandService.Transform.RotationX.Id,
                SceneDocumentCommandService.Transform.RotationY.Id,
                SceneDocumentCommandService.Transform.RotationZ.Id,
            ];
            fields.Add(new(role, "SunAzimuth", rotation));
            fields.Add(new(role, "SunElevation", rotation));
            fields.Add(new(role, "AngularSizeRadians", [SceneDocumentCommandService.DirectionalLight.AngularSizeRadians.Id]));
            fields.Add(new(role, "DiskScale", [SceneDocumentCommandService.DirectionalLight.AtmosphereDiskLuminanceScaleRgb.Id]));
            fields.Add(new(role, "UsePerPixelAtmosphereTransmittance", [SceneDocumentCommandService.DirectionalLight.UsePerPixelAtmosphereTransmittance.Id]));
        }

        return fields.AsReadOnly();
    }

    private static InspectorFieldPresentation Sky(string key, string pointer, string label, string? group = null, string aliases = "")
        => new(
            key,
            new PropertyId(SceneDocumentCommandService.SceneEnvironmentKind, pointer),
            InspectorPropertyScope.Environment,
            "SkyAtmosphere",
            group,
            label,
            "Sky Atmosphere Atmospheric sky and aerial perspective authored on the scene.",
            $"{key} {aliases}");

    private static InspectorFieldPresentation Exposure(
        string key,
        string pointer,
        string label,
        string? group = null,
        InspectorFieldApplicability applicability = InspectorFieldApplicability.Always,
        string aliases = "")
        => new(
            key,
            new PropertyId(SceneDocumentCommandService.SceneEnvironmentKind, $"/post_process/{pointer}"),
            InspectorPropertyScope.PostProcessing,
            "Exposure",
            group,
            label,
            "Exposure pipeline values from Oxygen's native PostProcessVolume.",
            $"{key} {aliases} {(applicability is InspectorFieldApplicability.AutoExposure or InspectorFieldApplicability.AutoExposureSpot ? "AutoExposure" : string.Empty)}",
            applicability);

    private static InspectorFieldPresentation Post(
        string key,
        string pointer,
        string label,
        string section,
        string description,
        InspectorFieldApplicability applicability = InspectorFieldApplicability.Always,
        string aliases = "")
        => new(
            key,
            new PropertyId(SceneDocumentCommandService.SceneEnvironmentKind, $"/post_process/{pointer}"),
            InspectorPropertyScope.PostProcessing,
            section,
group: null,
            label,
            $"{section} {description}",
            $"{key} {aliases}",
            applicability);
}

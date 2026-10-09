// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Interop.Input;
using Oxygen.Editor.World.Serialization;
using Oxygen.Interop.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Pure conversions at the mixed-mode transport boundary.</summary>
internal static class RuntimeTransportConversion
{
    /// <summary>Converts authored fog without changing its units.</summary>
    /// <param name="value">The authored fog.</param>
    /// <returns>The native transport value.</returns>
    public static FogEnvironmentManaged ToNative(FogEnvironmentData value)
        => new()
        {
            Enabled = value.Enabled,
            HeightFogEnabled = value.HeightFogEnabled,
            Density = value.Density,
            HeightFalloff = value.HeightFalloff,
            HeightOffsetMeters = value.HeightOffsetMeters,
            MaxOpacity = value.MaxOpacity,
            InscatteringLuminanceRgb = value.InscatteringLuminanceRgb,
            SkyAmbientScaleRgb = value.SkyAmbientScaleRgb,
            SecondDensity = value.SecondDensity,
            SecondHeightFalloff = value.SecondHeightFalloff,
            SecondHeightOffsetMeters = value.SecondHeightOffsetMeters,
            StartDistanceMeters = value.StartDistanceMeters,
            EndDistanceMeters = value.EndDistanceMeters,
            CutoffDistanceMeters = value.CutoffDistanceMeters,
            DirectionalInscatteringLuminanceRgb = value.DirectionalInscatteringLuminanceRgb,
            DirectionalInscatteringExponent = value.DirectionalInscatteringExponent,
            DirectionalInscatteringStartDistanceMeters = value.DirectionalInscatteringStartDistanceMeters,
            VolumetricFogEnabled = value.VolumetricFogEnabled,
            VolumetricScatteringDistribution = value.VolumetricScatteringDistribution,
            VolumetricAlbedoRgb = value.VolumetricAlbedoRgb,
            VolumetricEmissiveRgb = value.VolumetricEmissiveRgb,
            VolumetricExtinctionScale = value.VolumetricExtinctionScale,
            VolumetricDistanceMeters = value.VolumetricDistanceMeters,
            VolumetricStartDistanceMeters = value.VolumetricStartDistanceMeters,
            VolumetricNearFadeInDistanceMeters = value.VolumetricNearFadeInDistanceMeters,
            VolumetricStaticLightingScatteringIntensity = value.VolumetricStaticLightingScatteringIntensity,
            OverrideLightColorsWithFogInscattering = value.OverrideLightColorsWithFogInscattering,
            RenderInMainPass = value.RenderInMainPass,
            Holdout = value.Holdout,
            VisibleInReflectionCaptures = value.VisibleInReflectionCaptures,
            VisibleInRealTimeSkyCaptures = value.VisibleInRealTimeSkyCaptures,
        };

    /// <summary>Converts observed native fog without changing its units.</summary>
    /// <param name="value">The native transport value.</param>
    /// <returns>The observed fog.</returns>
    public static FogEnvironmentData FromNative(FogEnvironmentManaged value)
        => new()
        {
            Enabled = value.Enabled,
            HeightFogEnabled = value.HeightFogEnabled,
            Density = value.Density,
            HeightFalloff = value.HeightFalloff,
            HeightOffsetMeters = value.HeightOffsetMeters,
            MaxOpacity = value.MaxOpacity,
            InscatteringLuminanceRgb = value.InscatteringLuminanceRgb,
            SkyAmbientScaleRgb = value.SkyAmbientScaleRgb,
            SecondDensity = value.SecondDensity,
            SecondHeightFalloff = value.SecondHeightFalloff,
            SecondHeightOffsetMeters = value.SecondHeightOffsetMeters,
            StartDistanceMeters = value.StartDistanceMeters,
            EndDistanceMeters = value.EndDistanceMeters,
            CutoffDistanceMeters = value.CutoffDistanceMeters,
            DirectionalInscatteringLuminanceRgb = value.DirectionalInscatteringLuminanceRgb,
            DirectionalInscatteringExponent = value.DirectionalInscatteringExponent,
            DirectionalInscatteringStartDistanceMeters = value.DirectionalInscatteringStartDistanceMeters,
            VolumetricFogEnabled = value.VolumetricFogEnabled,
            VolumetricScatteringDistribution = value.VolumetricScatteringDistribution,
            VolumetricAlbedoRgb = value.VolumetricAlbedoRgb,
            VolumetricEmissiveRgb = value.VolumetricEmissiveRgb,
            VolumetricExtinctionScale = value.VolumetricExtinctionScale,
            VolumetricDistanceMeters = value.VolumetricDistanceMeters,
            VolumetricStartDistanceMeters = value.VolumetricStartDistanceMeters,
            VolumetricNearFadeInDistanceMeters = value.VolumetricNearFadeInDistanceMeters,
            VolumetricStaticLightingScatteringIntensity = value.VolumetricStaticLightingScatteringIntensity,
            OverrideLightColorsWithFogInscattering = value.OverrideLightColorsWithFogInscattering,
            RenderInMainPass = value.RenderInMainPass,
            Holdout = value.Holdout,
            VisibleInReflectionCaptures = value.VisibleInReflectionCaptures,
            VisibleInRealTimeSkyCaptures = value.VisibleInRealTimeSkyCaptures,
        };

    /// <summary>Converts an authored sky sphere and its resolved cubemap without changing units.</summary>
    /// <param name="value">The authored sky sphere.</param>
    /// <param name="cubemap">The resolved cubemap, or null for none.</param>
    /// <returns>The native transport value.</returns>
    public static SkySphereEnvironmentManaged ToNative(SkySphereEnvironmentData value, RuntimeTextureReference? cubemap)
        => new()
        {
            Enabled = value.Enabled,
            Source = (int)value.Source,
            Cubemap = ToNative(cubemap),
            SolidColorRgb = value.SolidColorRgb,
            IlluminanceLux = value.IlluminanceLux,
            Intensity = value.Intensity,
            RotationRadians = value.RotationRadians,
            TintRgb = value.TintRgb,
        };

    /// <summary>Converts an authored sky light and its resolved cubemap without changing units.</summary>
    /// <param name="value">The authored sky light.</param>
    /// <param name="cubemap">The resolved cubemap, or null for none.</param>
    /// <returns>The native transport value.</returns>
    public static SkyLightEnvironmentManaged ToNative(SkyLightEnvironmentData value, RuntimeTextureReference? cubemap)
        => new()
        {
            Enabled = value.Enabled,
            Source = (int)value.Source,
            Cubemap = ToNative(cubemap),
            CubemapIlluminanceLux = value.CubemapIlluminanceLux,
            Intensity = value.Intensity,
            TintRgb = value.TintRgb,
            DiffuseIntensity = value.DiffuseIntensity,
            SpecularIntensity = value.SpecularIntensity,
            CubemapAngleRadians = value.CubemapAngleRadians,
            LowerHemisphereColor = value.LowerHemisphereColor,
            LowerHemisphereIsSolidColor = value.LowerHemisphereIsSolidColor,
            LowerHemisphereBlendAlpha = value.LowerHemisphereBlendAlpha,
            VolumetricScatteringIntensity = value.VolumetricScatteringIntensity,
            AffectReflections = value.AffectReflections,
        };

    /// <summary>Converts an authored background without changing units.</summary>
    /// <param name="value">The authored background.</param>
    /// <returns>The native transport value.</returns>
    public static BackgroundEnvironmentManaged ToNative(BackgroundEnvironmentData value)
        => new() { Enabled = value.Enabled, ColorRgb = value.ColorRgb };

    /// <summary>Converts an observed native sky sphere; its cubemap is observed separately.</summary>
    /// <param name="value">The native transport value.</param>
    /// <returns>The observed sky sphere.</returns>
    public static SkySphereEnvironmentData FromNative(SkySphereEnvironmentManaged value)
        => new()
        {
            Enabled = value.Enabled,
            Source = (SkySphereSource)value.Source,
            SolidColorRgb = value.SolidColorRgb,
            IlluminanceLux = value.IlluminanceLux,
            Intensity = value.Intensity,
            RotationRadians = value.RotationRadians,
            TintRgb = value.TintRgb,
        };

    /// <summary>Converts an observed native sky light; its cubemap is observed separately.</summary>
    /// <param name="value">The native transport value.</param>
    /// <returns>The observed sky light.</returns>
    public static SkyLightEnvironmentData FromNative(SkyLightEnvironmentManaged value)
        => new()
        {
            Enabled = value.Enabled,
            Source = (SkyLightSource)value.Source,
            CubemapIlluminanceLux = value.CubemapIlluminanceLux,
            Intensity = value.Intensity,
            TintRgb = value.TintRgb,
            DiffuseIntensity = value.DiffuseIntensity,
            SpecularIntensity = value.SpecularIntensity,
            CubemapAngleRadians = value.CubemapAngleRadians,
            LowerHemisphereColor = value.LowerHemisphereColor,
            LowerHemisphereIsSolidColor = value.LowerHemisphereIsSolidColor,
            LowerHemisphereBlendAlpha = value.LowerHemisphereBlendAlpha,
            VolumetricScatteringIntensity = value.VolumetricScatteringIntensity,
            AffectReflections = value.AffectReflections,
        };

    /// <summary>Converts an observed native background.</summary>
    /// <param name="value">The native transport value.</param>
    /// <returns>The observed background.</returns>
    public static BackgroundEnvironmentData FromNative(BackgroundEnvironmentManaged value)
        => new() { Enabled = value.Enabled, ColorRgb = value.ColorRgb };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="entries">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static PropertyValueEntry[] ToNative(ImmutableArray<RuntimePropertyValue> entries)
        => [.. entries.Select(value => new PropertyValueEntry { ComponentId = value.ComponentId, FieldId = value.FieldId, Value = value.Value })];

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorKeyEventManaged ToNative(RuntimeKeyEvent value)
        => new() { key = ToNative(value.Key), pressed = value.Pressed, repeat = value.Repeat, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorButtonEventManaged ToNative(RuntimeButtonEvent value)
        => new() { button = ToNative(value.Button), pressed = value.Pressed, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorMouseMotionEventManaged ToNative(RuntimeMouseMotionEvent value)
        => new() { motion = value.Motion, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorMouseWheelEventManaged ToNative(RuntimeMouseWheelEvent value)
        => new() { scroll = value.Scroll, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static PlatformKey ToNative(RuntimeKey value)
    {
        if (!Enum.IsDefined(value))
        {
            throw new ArgumentOutOfRangeException(nameof(value));
        }

        // RuntimeKey deliberately fixes the native key ordinals. Boundary tests
        // compare every named value, including modifiers, to detect contract drift.
        return (PlatformKey)value;
    }

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static PlatformMouseButton ToNative(RuntimeMouseButton value) => value switch
    {
        RuntimeMouseButton.None => PlatformMouseButton.None,
        RuntimeMouseButton.Left => PlatformMouseButton.Left,
        RuntimeMouseButton.Right => PlatformMouseButton.Right,
        RuntimeMouseButton.Middle => PlatformMouseButton.Middle,
        RuntimeMouseButton.ExtButton1 => PlatformMouseButton.ExtButton1,
        RuntimeMouseButton.ExtButton2 => PlatformMouseButton.ExtButton2,
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };

    private static CubemapReferenceManaged ToNative(RuntimeTextureReference? cubemap)
        => cubemap is null
            ? default
            : new()
            {
                CookedRoot = cubemap.CookedRoot,
                DescriptorRelativePath = cubemap.DescriptorRelativePath,
                ProjectMount = cubemap.ProjectMount,
            };
}

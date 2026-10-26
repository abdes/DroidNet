// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Interop.Input;
using Oxygen.Interop.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Pure conversions at the mixed-mode transport boundary.</summary>
internal static class RuntimeTransportConversion
{
    // The native LodPolicyType values.
    private const byte FixedLodPolicy = 0;
    private const byte DistanceLodPolicy = 1;
    private const byte ScreenSpaceErrorLodPolicy = 2;

    /// <summary>Converts overrides to the native entries; each value fills the member of its kind.</summary>
    /// <param name="overrides">The overrides.</param>
    /// <returns>The native entries.</returns>
    public static OverrideEntryManaged[] ToNative(ImmutableArray<RuntimeOverride> overrides)
        => [.. overrides.Select(static entry => entry.Value switch
        {
            RuntimeOverrideValue.Flag flag => new OverrideEntryManaged { OverrideId = (ushort)entry.Id, Flag = flag.Value },
            RuntimeOverrideValue.Asset asset => new OverrideEntryManaged { OverrideId = (ushort)entry.Id, AssetPath = asset.Path },
            RuntimeOverrideValue.LodPolicyValue policy => new OverrideEntryManaged { OverrideId = (ushort)entry.Id, LodPolicy = ToNative(policy.Policy) },
            _ => throw new ArgumentOutOfRangeException(nameof(overrides), entry.Value, "Unknown override value kind."),
        })];

    /// <summary>Converts an instance change; its value keeps the schema's units.</summary>
    /// <param name="change">The change.</param>
    /// <returns>The native transport value.</returns>
    public static InstanceChangeManaged ToNative(RuntimeInstanceChange change)
    {
        ArgumentNullException.ThrowIfNull(change);
        var native = new InstanceChangeManaged { Path = ToNative(change.Path), OverrideId = (ushort)change.Id, PartId = change.PartId };
        switch (change.Value)
        {
            case RuntimeInstanceValue.Flag flag:
                native.Kind = InstanceValueKind.Bool;
                native.BoolValue = flag.Value;
                break;
            case RuntimeInstanceValue.Integer number:
                native.Kind = InstanceValueKind.Int32;
                native.Int32Value = number.Value;
                break;
            case RuntimeInstanceValue.Scalar number:
                native.Kind = InstanceValueKind.Float;
                native.FloatValue = number.Value;
                break;
            case RuntimeInstanceValue.Vector vector:
                native.Kind = InstanceValueKind.Vector3;
                native.Vector3Value = vector.Value;
                break;
            case RuntimeInstanceValue.Asset asset:
                native.Kind = InstanceValueKind.Asset;
                native.AssetPath = asset.Path;
                break;
            case RuntimeInstanceValue.LodPolicyValue policy:
                native.Kind = InstanceValueKind.LodPolicy;
                native.LodPolicy = ToNative(policy.Policy);
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(change), change.Value, "Unknown instance value kind.");
        }

        return native;
    }

    /// <summary>Converts a node path to its node IDs, shallowest first.</summary>
    /// <param name="path">The node path.</param>
    /// <returns>The native transport value.</returns>
    public static Guid[] ToNative(NodePath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return [.. path.Nodes.Select(static id => id.Value)];
    }

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
    /// <param name="cubemap">The cubemap texture, or null for none.</param>
    /// <returns>The native transport value.</returns>
    public static SkySphereEnvironmentManaged ToNative(SkySphereEnvironmentData value, AssetPath? cubemap)
        => new()
        {
            Enabled = value.Enabled,
            Source = (int)value.Source,
            CubemapPath = cubemap?.VirtualPath,
            SolidColorRgb = value.SolidColorRgb,
            IlluminanceLux = value.IlluminanceLux,
            Intensity = value.Intensity,
            RotationRadians = value.RotationRadians,
            TintRgb = value.TintRgb,
        };

    /// <summary>Converts an authored sky light and its resolved cubemap without changing units.</summary>
    /// <param name="value">The authored sky light.</param>
    /// <param name="cubemap">The cubemap texture, or null for none.</param>
    /// <returns>The native transport value.</returns>
    public static SkyLightEnvironmentManaged ToNative(SkyLightEnvironmentData value, AssetPath? cubemap)
        => new()
        {
            Enabled = value.Enabled,
            Source = (int)value.Source,
            CubemapPath = cubemap?.VirtualPath,
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
        => [.. entries.Select(ToNative)];

    /// <summary>Converts a managed field value without changing its identity, type or units.</summary>
    /// <param name="value">The managed field value.</param>
    /// <returns>The native transport value.</returns>
    public static PropertyValueEntry ToNative(RuntimePropertyValue value)
    {
        var entry = new PropertyValueEntry { ComponentId = (ushort)value.ComponentId, FieldId = value.FieldId };
        switch (value.Value)
        {
            case RuntimeFieldValue.Flag flag:
                entry.Type = FieldType.Bool;
                entry.BoolValue = flag.Value;
                break;
            case RuntimeFieldValue.Integer integer:
                entry.Type = FieldType.Int32;
                entry.Int32Value = integer.Value;
                break;
            case RuntimeFieldValue.Scalar scalar:
                entry.Type = FieldType.Float;
                entry.FloatValue = scalar.Value;
                break;
            default:
                throw new ArgumentException($"Field {value.FieldId} of component {value.ComponentId} has no value.", nameof(value));
        }

        return entry;
    }

    /// <summary>Converts an observed native field value without changing its identity, type or units.</summary>
    /// <param name="entry">The native transport value.</param>
    /// <returns>The managed field value.</returns>
    public static RuntimePropertyValue FromNative(PropertyValueEntry entry)
        => new(
            (RuntimeComponentId)entry.ComponentId,
            entry.FieldId,
            entry.Type switch
            {
                FieldType.Bool => new RuntimeFieldValue.Flag(entry.BoolValue),
                FieldType.Int32 => new RuntimeFieldValue.Integer(entry.Int32Value),
                FieldType.Float => new RuntimeFieldValue.Scalar(entry.FloatValue),
                _ => throw new ArgumentOutOfRangeException(nameof(entry), entry.Type, "Unknown field value type."),
            });

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

    /// <summary>Converts a property of a mounted scene's node; its value keeps the schema's units.</summary>
    /// <param name="property">The native property.</param>
    /// <returns>The property, or <see langword="null"/> for an asset no mounted source provides.</returns>
    public static RuntimeSceneNodeProperty? FromNative(SceneNodePropertyManaged property)
    {
        RuntimeInstanceValue? value = property.Kind switch
        {
            InstanceValueKind.Bool => new RuntimeInstanceValue.Flag(property.BoolValue),
            InstanceValueKind.Int32 => new RuntimeInstanceValue.Integer(property.Int32Value),
            InstanceValueKind.Float => new RuntimeInstanceValue.Scalar(property.FloatValue),
            InstanceValueKind.Vector3 => new RuntimeInstanceValue.Vector(property.Vector3Value),
            InstanceValueKind.Asset => string.IsNullOrEmpty(property.AssetPath) ? null : new RuntimeInstanceValue.Asset(property.AssetPath),
            InstanceValueKind.LodPolicy => new RuntimeInstanceValue.LodPolicyValue(FromNative(property.LodPolicy)),
            _ => throw new ArgumentOutOfRangeException(nameof(property), property.Kind, "Unknown property value kind."),
        };
        return value is null ? null : new RuntimeSceneNodeProperty((RuntimeOverrideId)property.OverrideId, property.PartId, property.LayoutRevision, value);
    }

    private static LodPolicy FromNative(LodPolicyManaged policy)
        => policy.Type switch
        {
            FixedLodPolicy => new LodPolicy.Fixed(policy.LodIndex),
            DistanceLodPolicy => new LodPolicy.Distance([.. policy.Thresholds ?? []], policy.HysteresisRatio),
            ScreenSpaceErrorLodPolicy => new LodPolicy.ScreenSpaceError([.. policy.EnterFinerSse ?? []], [.. policy.ExitCoarserSse ?? []]),
            _ => throw new ArgumentOutOfRangeException(nameof(policy), policy.Type, "Unknown LOD policy type."),
        };

    private static LodPolicyManaged ToNative(LodPolicy policy)
        => policy switch
        {
            LodPolicy.Fixed fixedPolicy => new LodPolicyManaged
            {
                Type = FixedLodPolicy,
                LodIndex = checked((byte)fixedPolicy.LodIndex),
            },
            LodPolicy.Distance distance => new LodPolicyManaged
            {
                Type = DistanceLodPolicy,
                Thresholds = [.. distance.Thresholds],
                HysteresisRatio = distance.HysteresisRatio,
            },
            LodPolicy.ScreenSpaceError error => new LodPolicyManaged
            {
                Type = ScreenSpaceErrorLodPolicy,
                EnterFinerSse = [.. error.EnterFinerSse],
                ExitCoarserSse = [.. error.ExitCoarserSse],
            },
            _ => throw new ArgumentOutOfRangeException(nameof(policy), policy, "Unknown LOD policy."),
        };
}

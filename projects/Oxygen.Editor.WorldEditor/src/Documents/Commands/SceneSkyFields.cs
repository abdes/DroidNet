// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// What shows behind the scene. The renderer shows one backdrop: an enabled
/// atmosphere, else an enabled background, else an enabled sky sphere.
/// </summary>
public enum EnvironmentBackdrop
{
    /// <summary>Nothing is enabled, so nothing shows behind the scene.</summary>
    None = 0,

    /// <summary>The procedural sky atmosphere.</summary>
    Atmosphere = 1,

    /// <summary>The sky sphere's cubemap.</summary>
    Cubemap = 2,

    /// <summary>A solid color, from the sky sphere when it lights the scene, else from the background.</summary>
    SolidColor = 3,
}

/// <summary>
/// Typed scene-environment property identities for the backdrop, the Sky Sphere and the Sky Light.
/// </summary>
/// <remarks>
/// The <c>/backdrop/</c> properties are editor views over the engine-shaped
/// atmosphere, sky sphere and background records: each write sets the enable
/// flags so that exactly the chosen backdrop shows.
/// </remarks>
internal static class SceneSkyFields
{
    /// <summary>Gets the property for what shows behind the scene.</summary>
    internal static PropertyId<EnvironmentBackdrop> Backdrop { get; } = Id<EnvironmentBackdrop>("/backdrop/mode");

    /// <summary>Gets the property for whether the solid color backdrop also lights the scene.</summary>
    internal static PropertyId<bool> SolidColorLightsScene { get; } = Id<bool>("/backdrop/solid_color_lights_scene");

    /// <summary>Gets the property for the solid color backdrop, in linear RGB.</summary>
    internal static PropertyId<Vector3> SolidColor { get; } = Id<Vector3>("/backdrop/solid_color");

    /// <summary>Gets the property for the sky sphere cubemap.</summary>
    internal static PropertyId<Uri?> SkySphereCubemap { get; } = Id<Uri?>("/sky_sphere/cubemap");

    /// <summary>Gets the property for the illuminance, in lux, the sky is calibrated to deliver; 0 keeps it raw.</summary>
    internal static PropertyId<float> SkySphereIlluminanceLux { get; } = Id<float>("/sky_sphere/illuminance_lux");

    /// <summary>Gets the property for the sky sphere radiance multiplier.</summary>
    internal static PropertyId<float> SkySphereIntensity { get; } = Id<float>("/sky_sphere/intensity");

    /// <summary>Gets the property for the sky sphere rotation, in radians.</summary>
    internal static PropertyId<float> SkySphereRotationRadians { get; } = Id<float>("/sky_sphere/rotation_radians");

    /// <summary>Gets the property for the sky sphere tint.</summary>
    internal static PropertyId<Vector3> SkySphereTintRgb { get; } = Id<Vector3>("/sky_sphere/tint_rgb");

    /// <summary>Gets the property for whether the sky lights the scene.</summary>
    internal static PropertyId<bool> SkyLightEnabled { get; } = Id<bool>("/sky_light/enabled");

    /// <summary>Gets the property for where the sky light takes its radiance from.</summary>
    internal static PropertyId<SkyLightSource> SkyLightSource { get; } = Id<SkyLightSource>("/sky_light/source");

    /// <summary>Gets the property for the sky light cubemap.</summary>
    internal static PropertyId<Uri?> SkyLightCubemap { get; } = Id<Uri?>("/sky_light/cubemap");

    /// <summary>Gets the property for the illuminance, in lux, the sky light cubemap is calibrated to deliver; 0 keeps it raw.</summary>
    internal static PropertyId<float> SkyLightCubemapIlluminanceLux { get; } = Id<float>("/sky_light/cubemap_illuminance_lux");

    /// <summary>Gets the property for the sky light multiplier.</summary>
    internal static PropertyId<float> SkyLightIntensity { get; } = Id<float>("/sky_light/intensity");

    /// <summary>Gets the property for the sky light tint.</summary>
    internal static PropertyId<Vector3> SkyLightTintRgb { get; } = Id<Vector3>("/sky_light/tint_rgb");

    /// <summary>Gets the property for the sky light diffuse multiplier.</summary>
    internal static PropertyId<float> SkyLightDiffuseIntensity { get; } = Id<float>("/sky_light/diffuse_intensity");

    /// <summary>Gets the property for the sky light specular multiplier.</summary>
    internal static PropertyId<float> SkyLightSpecularIntensity { get; } = Id<float>("/sky_light/specular_intensity");

    /// <summary>Gets the property for the sky light cubemap rotation, in radians.</summary>
    internal static PropertyId<float> SkyLightCubemapAngleRadians { get; } = Id<float>("/sky_light/cubemap_angle_radians");

    /// <summary>Gets the property for the lower hemisphere color.</summary>
    internal static PropertyId<Vector3> SkyLightLowerHemisphereColor { get; } = Id<Vector3>("/sky_light/lower_hemisphere_color");

    /// <summary>Gets the property for whether the lower hemisphere is replaced by its color.</summary>
    internal static PropertyId<bool> SkyLightLowerHemisphereIsSolidColor { get; } = Id<bool>("/sky_light/lower_hemisphere_is_solid_color");

    /// <summary>Gets the property for how much of the lower hemisphere its color replaces.</summary>
    internal static PropertyId<float> SkyLightLowerHemisphereBlendAlpha { get; } = Id<float>("/sky_light/lower_hemisphere_blend_alpha");

    /// <summary>Gets the property for the sky light scattered by volumetric fog.</summary>
    internal static PropertyId<float> SkyLightVolumetricScatteringIntensity { get; } = Id<float>("/sky_light/volumetric_scattering_intensity");

    /// <summary>Gets the property for whether the sky light contributes to reflections.</summary>
    internal static PropertyId<bool> SkyLightAffectReflections { get; } = Id<bool>("/sky_light/affect_reflections");

    /// <summary>Resolves the backdrop the renderer shows for an environment.</summary>
    /// <param name="value">The authored environment.</param>
    /// <returns>The shown backdrop.</returns>
    internal static EnvironmentBackdrop ResolveBackdrop(SceneEnvironmentData value)
        => value.AtmosphereEnabled ? EnvironmentBackdrop.Atmosphere
            : value.Background.Enabled ? EnvironmentBackdrop.SolidColor
            : !value.SkySphere.Enabled ? EnvironmentBackdrop.None
            : value.SkySphere.Source == SkySphereSource.Cubemap ? EnvironmentBackdrop.Cubemap
            : EnvironmentBackdrop.SolidColor;

    /// <summary>Gets whether the solid color backdrop lights the scene through sky light captures.</summary>
    /// <param name="value">The authored environment.</param>
    /// <returns><see langword="true"/> unless the background holds the color.</returns>
    internal static bool SolidColorLightsSceneOf(SceneEnvironmentData value) => !value.Background.Enabled;

    /// <summary>Gets the solid color backdrop from whichever record holds it.</summary>
    /// <param name="value">The authored environment.</param>
    /// <returns>The linear color.</returns>
    internal static Vector3 SolidColorOf(SceneEnvironmentData value)
        => value.Background.Enabled ? value.Background.ColorRgb : value.SkySphere.SolidColorRgb;

    /// <summary>Adds the backdrop, sky sphere and sky light descriptors.</summary>
    /// <param name="descriptors">The catalog being built.</param>
    internal static void AddDescriptors(List<PropertyDescriptor> descriptors)
    {
        descriptors.Add(Environment(
            Backdrop,
            ResolveBackdrop,
            WriteBackdrop,
            static value => value is EnvironmentBackdrop.Atmosphere or EnvironmentBackdrop.Cubemap or EnvironmentBackdrop.SolidColor
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.EnvironmentSkyInvalid, "Choose an atmosphere, a cubemap or a solid color backdrop."),
            "segmented"));
        descriptors.Add(Environment(SolidColorLightsScene, SolidColorLightsSceneOf, WriteSolidColorLightsScene, static _ => ValidationResult.Ok, "toggle"));
        descriptors.Add(Environment(SolidColor, SolidColorOf, WriteSolidColor, value => ValidateVector("Solid color", 0f, null, value), "color-rgb"));

        descriptors.Add(Sphere(SkySphereCubemap, static s => s.Cubemap, static (s, v) => s with { Cubemap = v }, static value => ValidateAssetUri("Sky sphere cubemap", value), "asset-picker"));
        descriptors.Add(Sphere(SkySphereIlluminanceLux, static s => s.IlluminanceLux, static (s, v) => s with { IlluminanceLux = v }, static value => ValidateFloat("Sky illuminance", 0f, null, value), "numberbox"));
        descriptors.Add(Sphere(SkySphereIntensity, static s => s.Intensity, static (s, v) => s with { Intensity = v }, static value => ValidateFloat("Sky sphere intensity", 0f, null, value), "numberbox"));
        descriptors.Add(Sphere(SkySphereRotationRadians, static s => s.RotationRadians, static (s, v) => s with { RotationRadians = v }, static value => ValidateFloat("Sky sphere rotation", null, null, value), "numberbox"));
        descriptors.Add(Sphere(SkySphereTintRgb, static s => s.TintRgb, static (s, v) => s with { TintRgb = v }, static value => ValidateVector("Sky sphere tint", 0f, null, value), "color-rgb"));

        descriptors.Add(Light(SkyLightEnabled, static s => s.Enabled, static (s, v) => s with { Enabled = v }, static _ => ValidationResult.Ok, "toggle"));
        descriptors.Add(Light(
            SkyLightSource,
            static s => s.Source,
            static (s, v) => s with { Source = v },
            static value => Enum.IsDefined(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.EnvironmentSkyInvalid, "Sky light source is not valid."),
            "segmented"));
        descriptors.Add(Light(SkyLightCubemap, static s => s.Cubemap, static (s, v) => s with { Cubemap = v }, static value => ValidateAssetUri("Sky light cubemap", value), "asset-picker"));
        descriptors.Add(Light(SkyLightCubemapIlluminanceLux, static s => s.CubemapIlluminanceLux, static (s, v) => s with { CubemapIlluminanceLux = v }, static value => ValidateFloat("Sky light cubemap illuminance", 0f, null, value), "numberbox"));
        descriptors.Add(Light(SkyLightIntensity, static s => s.Intensity, static (s, v) => s with { Intensity = v }, static value => ValidateFloat("Sky light intensity", 0f, null, value), "numberbox"));
        descriptors.Add(Light(SkyLightTintRgb, static s => s.TintRgb, static (s, v) => s with { TintRgb = v }, static value => ValidateVector("Sky light tint", 0f, null, value), "color-rgb"));
        descriptors.Add(Light(SkyLightDiffuseIntensity, static s => s.DiffuseIntensity, static (s, v) => s with { DiffuseIntensity = v }, static value => ValidateFloat("Sky light diffuse", 0f, null, value), "numberbox"));
        descriptors.Add(Light(SkyLightSpecularIntensity, static s => s.SpecularIntensity, static (s, v) => s with { SpecularIntensity = v }, static value => ValidateFloat("Sky light specular", 0f, null, value), "numberbox"));
        descriptors.Add(Light(SkyLightCubemapAngleRadians, static s => s.CubemapAngleRadians, static (s, v) => s with { CubemapAngleRadians = v }, static value => ValidateFloat("Sky light cubemap rotation", null, null, value), "numberbox"));
        descriptors.Add(Light(SkyLightLowerHemisphereColor, static s => s.LowerHemisphereColor, static (s, v) => s with { LowerHemisphereColor = v }, static value => ValidateVector("Lower hemisphere color", 0f, null, value), "color-rgb"));
        descriptors.Add(Light(SkyLightLowerHemisphereIsSolidColor, static s => s.LowerHemisphereIsSolidColor, static (s, v) => s with { LowerHemisphereIsSolidColor = v }, static _ => ValidationResult.Ok, "toggle"));
        descriptors.Add(Light(SkyLightLowerHemisphereBlendAlpha, static s => s.LowerHemisphereBlendAlpha, static (s, v) => s with { LowerHemisphereBlendAlpha = v }, static value => ValidateFloat("Lower hemisphere blend", 0f, 1f, value), "numberbox"));
        descriptors.Add(Light(SkyLightVolumetricScatteringIntensity, static s => s.VolumetricScatteringIntensity, static (s, v) => s with { VolumetricScatteringIntensity = v }, static value => ValidateFloat("Sky light volumetric scattering", 0f, null, value), "numberbox"));
        descriptors.Add(Light(SkyLightAffectReflections, static s => s.AffectReflections, static (s, v) => s with { AffectReflections = v }, static _ => ValidationResult.Ok, "toggle"));
    }

    /// <summary>Validates a complete sky sphere record.</summary>
    /// <param name="value">The candidate record.</param>
    /// <returns>The first problem, or <see langword="null"/> when valid.</returns>
    internal static string? ValidateSkySphere(SkySphereEnvironmentData value)
        => new[]
        {
            Enum.IsDefined(value.Source) ? ValidationResult.Ok : ValidationResult.Fail(SceneDiagnosticCodes.EnvironmentSkyInvalid, "Sky sphere source is not valid."),
            ValidateAssetUri("Sky sphere cubemap", value.Cubemap),
            ValidateVector("Solid color", 0f, null, value.SolidColorRgb),
            ValidateFloat("Sky illuminance", 0f, null, value.IlluminanceLux),
            ValidateFloat("Sky sphere intensity", 0f, null, value.Intensity),
            ValidateFloat("Sky sphere rotation", null, null, value.RotationRadians),
            ValidateVector("Sky sphere tint", 0f, null, value.TintRgb),
        }.Where(static result => !result.IsValid).Select(static result => result.Message).FirstOrDefault();

    /// <summary>Validates a complete sky light record.</summary>
    /// <param name="value">The candidate record.</param>
    /// <returns>The first problem, or <see langword="null"/> when valid.</returns>
    internal static string? ValidateSkyLight(SkyLightEnvironmentData value)
        => new[]
        {
            Enum.IsDefined(value.Source) ? ValidationResult.Ok : ValidationResult.Fail(SceneDiagnosticCodes.EnvironmentSkyInvalid, "Sky light source is not valid."),
            ValidateAssetUri("Sky light cubemap", value.Cubemap),
            ValidateFloat("Sky light cubemap illuminance", 0f, null, value.CubemapIlluminanceLux),
            ValidateFloat("Sky light intensity", 0f, null, value.Intensity),
            ValidateVector("Sky light tint", 0f, null, value.TintRgb),
            ValidateFloat("Sky light diffuse", 0f, null, value.DiffuseIntensity),
            ValidateFloat("Sky light specular", 0f, null, value.SpecularIntensity),
            ValidateFloat("Sky light cubemap rotation", null, null, value.CubemapAngleRadians),
            ValidateVector("Lower hemisphere color", 0f, null, value.LowerHemisphereColor),
            ValidateFloat("Lower hemisphere blend", 0f, 1f, value.LowerHemisphereBlendAlpha),
            ValidateFloat("Sky light volumetric scattering", 0f, null, value.VolumetricScatteringIntensity),
        }.Where(static result => !result.IsValid).Select(static result => result.Message).FirstOrDefault();

    /// <summary>Gets the overlay schema pointer for a sky property pointer.</summary>
    /// <param name="pointer">The property's JSON pointer.</param>
    /// <returns>The schema pointer, or <see langword="null"/> for a non-sky property.</returns>
    internal static string? SchemaPointer(string pointer)
        => pointer.StartsWith("/backdrop/", StringComparison.Ordinal)
            ? "#/definitions/editor_scene_environment/backdrop/" + pointer["/backdrop/".Length..]
            : pointer.StartsWith("/sky_sphere/", StringComparison.Ordinal)
            ? "#/definitions/sky_sphere_environment/" + SchemaKey(pointer["/sky_sphere/".Length..])
            : pointer.StartsWith("/sky_light/", StringComparison.Ordinal)
            ? "#/definitions/sky_light_environment/" + SchemaKey(pointer["/sky_light/".Length..])
            : null;

    private static string SchemaKey(string key)
        => key switch
        {
            "cubemap" => "cubemap_ref",
            "cubemap_angle_radians" => "source_cubemap_angle_radians",
            "cubemap_illuminance_lux" => "illuminance_lux",
            _ => key,
        };

    private static SceneEnvironmentData WriteBackdrop(SceneEnvironmentData value, EnvironmentBackdrop next)
        => next switch
        {
            EnvironmentBackdrop.Atmosphere => value with
            {
                AtmosphereEnabled = true,
                SkySphere = value.SkySphere with { Enabled = false },
                Background = value.Background with { Enabled = false },
            },
            EnvironmentBackdrop.Cubemap => value with
            {
                AtmosphereEnabled = false,
                SkySphere = value.SkySphere with { Enabled = true, Source = SkySphereSource.Cubemap },
                Background = value.Background with { Enabled = false },
            },
            EnvironmentBackdrop.SolidColor when ResolveBackdrop(value) == EnvironmentBackdrop.SolidColor => value,
            EnvironmentBackdrop.SolidColor => value with
            {
                AtmosphereEnabled = false,
                SkySphere = value.SkySphere with { Enabled = true, Source = SkySphereSource.SolidColor },
                Background = value.Background with { Enabled = false },
            },
            _ => value,
        };

    private static SceneEnvironmentData WriteSolidColorLightsScene(SceneEnvironmentData value, bool lightsScene)
    {
        if (SolidColorLightsSceneOf(value) == lightsScene)
        {
            return value;
        }

        var color = SolidColorOf(value);
        return lightsScene
            ? value with
            {
                SkySphere = value.SkySphere with { Enabled = true, Source = SkySphereSource.SolidColor, SolidColorRgb = color },
                Background = value.Background with { Enabled = false },
            }
            : value with
            {
                SkySphere = value.SkySphere with { Enabled = false },
                Background = value.Background with { Enabled = true, ColorRgb = ClampDisplayColor(color) },
            };
    }

    private static SceneEnvironmentData WriteSolidColor(SceneEnvironmentData value, Vector3 color)
        => value.Background.Enabled
            ? value with { Background = value.Background with { ColorRgb = ClampDisplayColor(color) } }
            : value with { SkySphere = value.SkySphere with { SolidColorRgb = color } };

    // The background is a display-only SDR color; out-of-range values clamp.
    private static Vector3 ClampDisplayColor(Vector3 color) => Vector3.Clamp(color, Vector3.Zero, Vector3.One);

    private static PropertyId<T> Id<T>(string pointer) => new(SceneDocumentCommandService.SceneEnvironmentKind, pointer);

    private static PropertyDescriptor<T> Environment<T>(
        PropertyId<T> id,
        Func<SceneEnvironmentData, T> read,
        Func<SceneEnvironmentData, T, SceneEnvironmentData> write,
        Func<T, ValidationResult> validator,
        string renderer)
        => new(
            id: id,
            reader: target => read(Target(target).Value),
            writer: (target, value) => Target(target).Value = write(Target(target).Value, value),
            validator: validator,
            annotation: SceneEditorSchemaAnnotations.Get(SchemaPointer(id.Id.JsonPointer)!, new EditorAnnotation { Group = "Environment", Renderer = renderer }),
            engineCommandKey: $"environment{id.Id.JsonPointer.Replace('/', '.')}");

    private static PropertyDescriptor<T> Sphere<T>(
        PropertyId<T> id,
        Func<SkySphereEnvironmentData, T> read,
        Func<SkySphereEnvironmentData, T, SkySphereEnvironmentData> write,
        Func<T, ValidationResult> validator,
        string renderer)
        => Environment(id, value => read(value.SkySphere), (value, next) => value with { SkySphere = write(value.SkySphere, next) }, validator, renderer);

    private static PropertyDescriptor<T> Light<T>(
        PropertyId<T> id,
        Func<SkyLightEnvironmentData, T> read,
        Func<SkyLightEnvironmentData, T, SkyLightEnvironmentData> write,
        Func<T, ValidationResult> validator,
        string renderer)
        => Environment(id, value => read(value.SkyLight), (value, next) => value with { SkyLight = write(value.SkyLight, next) }, validator, renderer);

    private static SceneDocumentCommandService.SceneEnvironmentPropertyTarget Target(object target)
        => (SceneDocumentCommandService.SceneEnvironmentPropertyTarget)target;

    private static ValidationResult ValidateAssetUri(string label, Uri? value)
        => value is null || (value.IsAbsoluteUri && string.Equals(value.Scheme, "asset", StringComparison.OrdinalIgnoreCase))
            ? ValidationResult.Ok
            : ValidationResult.Fail(SceneDiagnosticCodes.EnvironmentSkyInvalid, $"{label} must be an absolute asset URI or empty.");

    private static ValidationResult ValidateVector(string label, float? minimum, float? maximum, Vector3 value)
    {
        var x = ValidateFloat(label, minimum, maximum, value.X);
        if (!x.IsValid)
        {
            return x;
        }

        var y = ValidateFloat(label, minimum, maximum, value.Y);
        return y.IsValid ? ValidateFloat(label, minimum, maximum, value.Z) : y;
    }

    private static ValidationResult ValidateFloat(string label, float? minimum, float? maximum, float value)
        => !float.IsFinite(value)
            ? ValidationResult.Fail(SceneDiagnosticCodes.EnvironmentSkyInvalid, $"{label} must be finite.")
            : (minimum, maximum) switch
            {
                ({ } low, { } high) when value < low || value > high => ValidationResult.Fail(
                    SceneDiagnosticCodes.EnvironmentSkyInvalid,
                    string.Create(CultureInfo.InvariantCulture, $"{label} must be between {low} and {high}.")),
                ({ } low, null) when value < low => ValidationResult.Fail(
                    SceneDiagnosticCodes.EnvironmentSkyInvalid,
                    string.Create(CultureInfo.InvariantCulture, $"{label} must be at least {low}.")),
                _ => ValidationResult.Ok,
            };
}

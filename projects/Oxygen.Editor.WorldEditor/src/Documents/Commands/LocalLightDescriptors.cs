// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Descriptor catalog for point or spot light inspector properties.
/// </summary>
/// <remarks>
/// Point and spot lights share the common light, shadow and emission fields; only spot lights
/// carry the cone angles. One catalog is built per component kind so property identities remain
/// kind-qualified.
/// </remarks>
internal sealed class LocalLightDescriptors
{
    private readonly List<PropertyDescriptor> all = [];

    private LocalLightDescriptors(string kind, bool hasCone)
    {
        this.Kind = kind;
        var definition = hasCone ? "spot_light" : "point_light";
        this.ColorDescriptor = this.Add(new PropertyDescriptor<Vector3>(
            id: new PropertyId<Vector3>(kind, "/color"),
            reader: static target => ((LightComponent)target).Color,
            writer: static (target, value) => ((LightComponent)target).Color = value,
            validator: static value => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z) && value.X >= 0 && value.Y >= 0 && value.Z >= 0
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.LocalLightInvalid, "Color must be finite and nonnegative."),
            annotation: SceneEditorSchemaAnnotations.Get("#/definitions/light_common/color_rgb", new EditorAnnotation { Group = "Light", Label = "Color", Renderer = "color-rgb" }),
            engineCommandKey: "light.color"));
        this.AffectsWorldDescriptor = this.Add(Bool(kind, "/affects_world", "#/definitions/light_common/affects_world", "Affects World", static light => light.AffectsWorld, static (light, value) => light.AffectsWorld = value));
        this.CastsShadowsDescriptor = this.Add(Bool(kind, "/casts_shadows", "#/definitions/light_common/casts_shadows", "Cast Shadows", static light => light.CastsShadows, static (light, value) => light.CastsShadows = value));
        this.ExposureCompensationDescriptor = this.Add(Float(kind, "/exposure_compensation", "#/definitions/light_common/exposure_compensation_ev", "Exposure Compensation", static light => light.ExposureCompensation, static (light, value) => light.ExposureCompensation = value, Finite));
        this.ShadowBiasDescriptor = this.Add(Float(kind, "/shadow/bias", "#/definitions/light_shadow/bias", "Shadow Bias", static light => light.ShadowBias, static (light, value) => light.ShadowBias = value, Nonnegative));
        this.ShadowNormalBiasDescriptor = this.Add(Float(kind, "/shadow/normal_bias", "#/definitions/light_shadow/normal_bias", "Normal Bias", static light => light.ShadowNormalBias, static (light, value) => light.ShadowNormalBias = value, Nonnegative));
        this.ContactShadowsDescriptor = this.Add(Bool(kind, "/shadow/contact_shadows", "#/definitions/light_shadow/contact_shadows", "Contact Shadows", static light => light.ContactShadows, static (light, value) => light.ContactShadows = value));
        this.ShadowResolutionHintDescriptor = this.Add(new PropertyDescriptor<ShadowResolutionHint>(
            id: new PropertyId<ShadowResolutionHint>(kind, "/shadow/resolution_hint"),
            reader: static target => ((LightComponent)target).ShadowResolutionHint,
            writer: static (target, value) => ((LightComponent)target).ShadowResolutionHint = value,
            validator: static value => Enum.IsDefined(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail(SceneDiagnosticCodes.LocalLightInvalid, "Unknown shadow resolution hint."),
            annotation: SceneEditorSchemaAnnotations.Get("#/definitions/light_shadow/resolution_hint", new EditorAnnotation { Group = "Shadows", Label = "Resolution", Renderer = "combo" }),
            engineCommandKey: "light.shadow.resolution_hint"));
        this.LuminousFluxLumensDescriptor = this.Add(Float(kind, "/luminous_flux_lm", $"#/definitions/{definition}/luminous_flux_lm", "Luminous Flux", ReadFlux, WriteFlux, Nonnegative));
        this.RangeDescriptor = this.Add(Float(kind, "/range", $"#/definitions/{definition}/range", "Range", ReadRange, WriteRange, Positive));
        this.SourceRadiusDescriptor = this.Add(Float(kind, "/source_radius", $"#/definitions/{definition}/source_radius", "Source Radius", ReadSourceRadius, WriteSourceRadius, Nonnegative));
        if (hasCone)
        {
            this.InnerConeAngleRadiansDescriptor = this.Add(Float(kind, "/inner_cone_angle_radians", "#/definitions/spot_light/inner_cone_angle_radians", "Inner Cone Angle", static light => ((SpotLightComponent)light).InnerConeAngleRadians, static (light, value) => ((SpotLightComponent)light).InnerConeAngleRadians = value, ConeAngle));
            this.OuterConeAngleRadiansDescriptor = this.Add(Float(kind, "/outer_cone_angle_radians", "#/definitions/spot_light/outer_cone_angle_radians", "Outer Cone Angle", static light => ((SpotLightComponent)light).OuterConeAngleRadians, static (light, value) => ((SpotLightComponent)light).OuterConeAngleRadians = value, ConeAngle));
        }

        this.ById = this.all.ToDictionary(static descriptor => descriptor.Id);
    }

    /// <summary>Gets the component kind that qualifies every property id.</summary>
    internal string Kind { get; }

    /// <summary>Gets the light color descriptor.</summary>
    internal PropertyDescriptor<Vector3> ColorDescriptor { get; }

    /// <summary>Gets the world-contribution descriptor.</summary>
    internal PropertyDescriptor<bool> AffectsWorldDescriptor { get; }

    /// <summary>Gets the shadow-casting descriptor.</summary>
    internal PropertyDescriptor<bool> CastsShadowsDescriptor { get; }

    /// <summary>Gets the exposure compensation descriptor.</summary>
    internal PropertyDescriptor<float> ExposureCompensationDescriptor { get; }

    /// <summary>Gets the shadow depth bias descriptor.</summary>
    internal PropertyDescriptor<float> ShadowBiasDescriptor { get; }

    /// <summary>Gets the shadow normal bias descriptor.</summary>
    internal PropertyDescriptor<float> ShadowNormalBiasDescriptor { get; }

    /// <summary>Gets the contact shadows descriptor.</summary>
    internal PropertyDescriptor<bool> ContactShadowsDescriptor { get; }

    /// <summary>Gets the shadow resolution hint descriptor.</summary>
    internal PropertyDescriptor<ShadowResolutionHint> ShadowResolutionHintDescriptor { get; }

    /// <summary>Gets the luminous flux descriptor.</summary>
    internal PropertyDescriptor<float> LuminousFluxLumensDescriptor { get; }

    /// <summary>Gets the range descriptor.</summary>
    internal PropertyDescriptor<float> RangeDescriptor { get; }

    /// <summary>Gets the source radius descriptor.</summary>
    internal PropertyDescriptor<float> SourceRadiusDescriptor { get; }

    /// <summary>Gets the inner cone angle descriptor; spot lights only.</summary>
    internal PropertyDescriptor<float>? InnerConeAngleRadiansDescriptor { get; }

    /// <summary>Gets the outer cone angle descriptor; spot lights only.</summary>
    internal PropertyDescriptor<float>? OuterConeAngleRadiansDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    internal IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Builds the point light descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    internal static LocalLightDescriptors BuildPoint() => new(SceneDocumentCommandService.PointLightKind, hasCone: false);

    /// <summary>Builds the spot light descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    internal static LocalLightDescriptors BuildSpot() => new(SceneDocumentCommandService.SpotLightKind, hasCone: true);

    private static ValidationResult Finite(float value)
        => float.IsFinite(value)
            ? ValidationResult.Ok
            : ValidationResult.Fail(SceneDiagnosticCodes.LocalLightInvalid, "Light values must be finite numbers.");

    private static ValidationResult Nonnegative(float value)
        => float.IsFinite(value) && value >= 0f
            ? ValidationResult.Ok
            : ValidationResult.Fail(SceneDiagnosticCodes.LocalLightInvalid, "Light values must be finite and nonnegative.");

    private static ValidationResult Positive(float value)
        => float.IsFinite(value) && value > 0f
            ? ValidationResult.Ok
            : ValidationResult.Fail(SceneDiagnosticCodes.LocalLightInvalid, "Range must be finite and greater than zero.");

    private static ValidationResult ConeAngle(float value)
        => float.IsFinite(value) && value is >= 0f and <= MathF.PI / 2f
            ? ValidationResult.Ok
            : ValidationResult.Fail(SceneDiagnosticCodes.LocalLightInvalid, "Cone angles must be between 0 and 90 degrees.");

    private static float ReadFlux(LightComponent light)
        => light switch
        {
            PointLightComponent point => point.LuminousFluxLumens,
            SpotLightComponent spot => spot.LuminousFluxLumens,
            _ => throw new ArgumentException("Not a point or spot light.", nameof(light)),
        };

    private static void WriteFlux(LightComponent light, float value)
    {
        switch (light)
        {
            case PointLightComponent point: point.LuminousFluxLumens = value; break;
            case SpotLightComponent spot: spot.LuminousFluxLumens = value; break;
            default: throw new ArgumentException("Not a point or spot light.", nameof(light));
        }
    }

    private static float ReadRange(LightComponent light)
        => light switch
        {
            PointLightComponent point => point.Range,
            SpotLightComponent spot => spot.Range,
            _ => throw new ArgumentException("Not a point or spot light.", nameof(light)),
        };

    private static void WriteRange(LightComponent light, float value)
    {
        switch (light)
        {
            case PointLightComponent point: point.Range = value; break;
            case SpotLightComponent spot: spot.Range = value; break;
            default: throw new ArgumentException("Not a point or spot light.", nameof(light));
        }
    }

    private static float ReadSourceRadius(LightComponent light)
        => light switch
        {
            PointLightComponent point => point.SourceRadius,
            SpotLightComponent spot => spot.SourceRadius,
            _ => throw new ArgumentException("Not a point or spot light.", nameof(light)),
        };

    private static void WriteSourceRadius(LightComponent light, float value)
    {
        switch (light)
        {
            case PointLightComponent point: point.SourceRadius = value; break;
            case SpotLightComponent spot: spot.SourceRadius = value; break;
            default: throw new ArgumentException("Not a point or spot light.", nameof(light));
        }
    }

    private static PropertyDescriptor<float> Float(
        string kind,
        string pointer,
        string schemaPointer,
        string label,
        Func<LightComponent, float> read,
        Action<LightComponent, float> write,
        Func<float, ValidationResult> validate)
        => new(
            id: new PropertyId<float>(kind, pointer),
            reader: target => read((LightComponent)target),
            writer: (target, value) => write((LightComponent)target, value),
            validator: validate,
            annotation: SceneEditorSchemaAnnotations.Get(schemaPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }),
            engineCommandKey: "light" + pointer.Replace('/', '.'));

    private static PropertyDescriptor<bool> Bool(
        string kind,
        string pointer,
        string schemaPointer,
        string label,
        Func<LightComponent, bool> read,
        Action<LightComponent, bool> write)
        => new(
            id: new PropertyId<bool>(kind, pointer),
            reader: target => read((LightComponent)target),
            writer: (target, value) => write((LightComponent)target, value),
            validator: static _ => ValidationResult.Ok,
            annotation: SceneEditorSchemaAnnotations.Get(schemaPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "toggle" }),
            engineCommandKey: "light" + pointer.Replace('/', '.'));

    private T Add<T>(T descriptor)
        where T : PropertyDescriptor
    {
        this.all.Add(descriptor);
        return descriptor;
    }
}

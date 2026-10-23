// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for point or spot light inspector properties.
/// </summary>
/// <remarks>
/// Point and spot lights share the common light, shadow and emission fields; only spot lights
/// carry the cone angles. One catalog is built per component kind so property identities remain
/// kind-qualified.
/// </remarks>
public sealed class LocalLightDescriptors
{
    private readonly List<PropertyDescriptor> all = [];

    private LocalLightDescriptors(bool hasCone)
    {
        var definition = hasCone ? "spot_light" : "point_light";
        this.ColorDescriptor = this.Add(new PropertyDescriptor<Vector3>(
            id: hasCone ? SceneSchema.SpotLight.Color : SceneSchema.PointLight.Color,
            reader: static target => ((LightComponent)target).Color,
            writer: static (target, value) => ((LightComponent)target).Color = value,
            validator: PropertyValidators.Of(hasCone ? SceneSchema.SpotLight.Color : SceneSchema.PointLight.Color, "Color"),
            annotation: SceneEditorSchemaAnnotations.Get("#/definitions/light_common/color_rgb", new EditorAnnotation { Group = "Light", Label = "Color", Renderer = "color-rgb" })));
        this.AffectsWorldDescriptor = this.Add(Bool(hasCone ? SceneSchema.SpotLight.AffectsWorld : SceneSchema.PointLight.AffectsWorld, "#/definitions/light_common/affects_world", "Affects World", static light => light.AffectsWorld, static (light, value) => light.AffectsWorld = value));
        this.CastsShadowsDescriptor = this.Add(Bool(hasCone ? SceneSchema.SpotLight.CastsShadows : SceneSchema.PointLight.CastsShadows, "#/definitions/light_common/casts_shadows", "Cast Shadows", static light => light.CastsShadows, static (light, value) => light.CastsShadows = value));
        this.ExposureCompensationDescriptor = this.Add(Float(hasCone ? SceneSchema.SpotLight.ExposureCompensation : SceneSchema.PointLight.ExposureCompensation, "#/definitions/light_common/exposure_compensation_ev", "Exposure Compensation", static light => light.ExposureCompensation, static (light, value) => light.ExposureCompensation = value));
        this.ShadowBiasDescriptor = this.Add(Float(hasCone ? SceneSchema.SpotLight.ShadowBias : SceneSchema.PointLight.ShadowBias, "#/definitions/light_shadow/bias", "Shadow Bias", static light => light.ShadowBias, static (light, value) => light.ShadowBias = value));
        this.ShadowNormalBiasDescriptor = this.Add(Float(hasCone ? SceneSchema.SpotLight.ShadowNormalBias : SceneSchema.PointLight.ShadowNormalBias, "#/definitions/light_shadow/normal_bias", "Normal Bias", static light => light.ShadowNormalBias, static (light, value) => light.ShadowNormalBias = value));
        this.ContactShadowsDescriptor = this.Add(Bool(hasCone ? SceneSchema.SpotLight.ContactShadows : SceneSchema.PointLight.ContactShadows, "#/definitions/light_shadow/contact_shadows", "Contact Shadows", static light => light.ContactShadows, static (light, value) => light.ContactShadows = value));
        this.ShadowResolutionHintDescriptor = this.Add(new PropertyDescriptor<ShadowResolutionHint>(
            id: hasCone ? SceneSchema.SpotLight.ShadowResolutionHint : SceneSchema.PointLight.ShadowResolutionHint,
            reader: static target => ((LightComponent)target).ShadowResolutionHint,
            writer: static (target, value) => ((LightComponent)target).ShadowResolutionHint = value,
            validator: PropertyValidators.Defined<ShadowResolutionHint>("Resolution"),
            annotation: SceneEditorSchemaAnnotations.Get("#/definitions/light_shadow/resolution_hint", new EditorAnnotation { Group = "Shadows", Label = "Resolution", Renderer = "combo" })));
        this.LuminousFluxLumensDescriptor = this.Add(Float(hasCone ? SceneSchema.SpotLight.LuminousFluxLumens : SceneSchema.PointLight.LuminousFluxLumens, $"#/definitions/{definition}/luminous_flux_lm", "Luminous Flux", ReadFlux, WriteFlux));
        this.RangeDescriptor = this.Add(Float(hasCone ? SceneSchema.SpotLight.Range : SceneSchema.PointLight.Range, $"#/definitions/{definition}/range", "Range", ReadRange, WriteRange));
        this.SourceRadiusDescriptor = this.Add(Float(hasCone ? SceneSchema.SpotLight.SourceRadius : SceneSchema.PointLight.SourceRadius, $"#/definitions/{definition}/source_radius", "Source Radius", ReadSourceRadius, WriteSourceRadius));
        if (hasCone)
        {
            this.InnerConeAngleRadiansDescriptor = this.Add(Float(SceneSchema.SpotLight.InnerConeAngleRadians, "#/definitions/spot_light/inner_cone_angle_radians", "Inner Cone Angle", static light => ((SpotLightComponent)light).InnerConeAngleRadians, static (light, value) => ((SpotLightComponent)light).InnerConeAngleRadians = value));
            this.OuterConeAngleRadiansDescriptor = this.Add(Float(SceneSchema.SpotLight.OuterConeAngleRadians, "#/definitions/spot_light/outer_cone_angle_radians", "Outer Cone Angle", static light => ((SpotLightComponent)light).OuterConeAngleRadians, static (light, value) => ((SpotLightComponent)light).OuterConeAngleRadians = value));
        }

        this.ById = this.all.ToDictionary(static descriptor => descriptor.Id);
    }

    /// <summary>Gets the light color descriptor.</summary>
    public PropertyDescriptor<Vector3> ColorDescriptor { get; }

    /// <summary>Gets the world-contribution descriptor.</summary>
    public PropertyDescriptor<bool> AffectsWorldDescriptor { get; }

    /// <summary>Gets the shadow-casting descriptor.</summary>
    public PropertyDescriptor<bool> CastsShadowsDescriptor { get; }

    /// <summary>Gets the exposure compensation descriptor.</summary>
    public PropertyDescriptor<float> ExposureCompensationDescriptor { get; }

    /// <summary>Gets the shadow depth bias descriptor.</summary>
    public PropertyDescriptor<float> ShadowBiasDescriptor { get; }

    /// <summary>Gets the shadow normal bias descriptor.</summary>
    public PropertyDescriptor<float> ShadowNormalBiasDescriptor { get; }

    /// <summary>Gets the contact shadows descriptor.</summary>
    public PropertyDescriptor<bool> ContactShadowsDescriptor { get; }

    /// <summary>Gets the shadow resolution hint descriptor.</summary>
    public PropertyDescriptor<ShadowResolutionHint> ShadowResolutionHintDescriptor { get; }

    /// <summary>Gets the luminous flux descriptor.</summary>
    public PropertyDescriptor<float> LuminousFluxLumensDescriptor { get; }

    /// <summary>Gets the range descriptor.</summary>
    public PropertyDescriptor<float> RangeDescriptor { get; }

    /// <summary>Gets the source radius descriptor.</summary>
    public PropertyDescriptor<float> SourceRadiusDescriptor { get; }

    /// <summary>Gets the inner cone angle descriptor; spot lights only.</summary>
    public PropertyDescriptor<float>? InnerConeAngleRadiansDescriptor { get; }

    /// <summary>Gets the outer cone angle descriptor; spot lights only.</summary>
    public PropertyDescriptor<float>? OuterConeAngleRadiansDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Builds the point light descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    public static LocalLightDescriptors BuildPoint() => new(hasCone: false);

    /// <summary>Builds the spot light descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    public static LocalLightDescriptors BuildSpot() => new(hasCone: true);

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
        PropertyId<float> id,
        string schemaPointer,
        string label,
        Func<LightComponent, float> read,
        Action<LightComponent, float> write)
        => new(
            id: id,
            reader: target => read((LightComponent)target),
            writer: (target, value) => write((LightComponent)target, value),
            validator: PropertyValidators.Of(id, label),
            annotation: SceneEditorSchemaAnnotations.Get(schemaPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "numberbox", Step = 0.01 }));

    private static PropertyDescriptor<bool> Bool(
        PropertyId<bool> id,
        string schemaPointer,
        string label,
        Func<LightComponent, bool> read,
        Action<LightComponent, bool> write)
        => new(
            id: id,
            reader: target => read((LightComponent)target),
            writer: (target, value) => write((LightComponent)target, value),
            validator: static _ => ValidationResult.Ok,
            annotation: SceneEditorSchemaAnnotations.Get(schemaPointer, new EditorAnnotation { Group = "Light", Label = label, Renderer = "toggle" }));

    private T Add<T>(T descriptor)
        where T : PropertyDescriptor
    {
        this.all.Add(descriptor);
        return descriptor;
    }
}

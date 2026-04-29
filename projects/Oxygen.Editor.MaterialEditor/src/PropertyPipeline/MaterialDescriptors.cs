// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;
using Oxygen.Assets.Import.Materials;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Schema-driven property descriptors for <c>oxygen.material-descriptor.schema.json</c>.
/// </summary>
/// <remarks>
/// <para>
/// Each descriptor's <see cref="PropertyId"/> is the JSON Pointer of the
/// field inside the engine schema (e.g. <c>/parameters/metalness</c>,
/// <c>/parameters/base_color/0</c>). That makes the descriptor table
/// directly addressable from authored material JSON.
/// </para>
/// <para>
/// The reader/writer pair maps engine-schema pointers to the editor's
/// glTF-style <see cref="MaterialSource"/> model so the existing
/// in-memory document model is preserved unchanged. This is the proof
/// that the design accepts a different runtime shape on the editor side
/// without disturbing the canonical authoring schema used by cooking.
/// </para>
/// </remarks>
public sealed class MaterialDescriptors
{
    /// <summary>
    /// The component kind id for material property identities.
    /// </summary>
    public const string Kind = "material";

    private MaterialDescriptors(IReadOnlyDictionary<PropertyId, PropertyDescriptor> byId)
    {
        this.ById = byId;
    }

    /// <summary>
    /// Gets the descriptor table, indexed by <see cref="PropertyId"/>.
    /// </summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>The base color red channel descriptor.</summary>
    public static PropertyId<float> BaseColorR { get; } = new(Kind, "/parameters/base_color/0");

    /// <summary>The base color green channel descriptor.</summary>
    public static PropertyId<float> BaseColorG { get; } = new(Kind, "/parameters/base_color/1");

    /// <summary>The base color blue channel descriptor.</summary>
    public static PropertyId<float> BaseColorB { get; } = new(Kind, "/parameters/base_color/2");

    /// <summary>The base color alpha channel descriptor.</summary>
    public static PropertyId<float> BaseColorA { get; } = new(Kind, "/parameters/base_color/3");

    /// <summary>The metalness scalar descriptor.</summary>
    public static PropertyId<float> Metalness { get; } = new(Kind, "/parameters/metalness");

    /// <summary>The roughness scalar descriptor.</summary>
    public static PropertyId<float> Roughness { get; } = new(Kind, "/parameters/roughness");

    /// <summary>The ambient occlusion scalar descriptor.</summary>
    public static PropertyId<float> AmbientOcclusion { get; } = new(Kind, "/parameters/ambient_occlusion");

    /// <summary>The alpha cutoff descriptor.</summary>
    public static PropertyId<float> AlphaCutoff { get; } = new(Kind, "/parameters/alpha_cutoff");

    /// <summary>The normal texture scale descriptor.</summary>
    public static PropertyId<float> NormalScale { get; } = new(Kind, "/parameters/normal_scale");

    /// <summary>The double-sided flag descriptor.</summary>
    public static PropertyId<bool> DoubleSided { get; } = new(Kind, "/parameters/double_sided");

    /// <summary>The alpha mode descriptor.</summary>
    public static PropertyId<MaterialAlphaMode> AlphaMode { get; } = new(Kind, "/alpha_mode");

    /// <summary>The shared catalog instance.</summary>
    public static MaterialDescriptors Catalog { get; } = Build();

    private static MaterialDescriptors Build()
    {
        var unitFloat = new EditorAnnotation { Group = "Parameters", Renderer = "slider", Step = 0.01, SoftMin = 0.0, SoftMax = 1.0 };
        var byId = new Dictionary<PropertyId, PropertyDescriptor>();

        Register(byId, BaseColorR, unitFloat with { Renderer = "color-rgba" }, "material.base_color.r",
            static state => state.Source.PbrMetallicRoughness.BaseColorR,
            static (state, v) => state.ReplacePbr(p => Pbr(p, baseColorR: v)),
            ValidateUnitFloat);
        Register(byId, BaseColorG, unitFloat with { Renderer = "color-rgba" }, "material.base_color.g",
            static state => state.Source.PbrMetallicRoughness.BaseColorG,
            static (state, v) => state.ReplacePbr(p => Pbr(p, baseColorG: v)),
            ValidateUnitFloat);
        Register(byId, BaseColorB, unitFloat with { Renderer = "color-rgba" }, "material.base_color.b",
            static state => state.Source.PbrMetallicRoughness.BaseColorB,
            static (state, v) => state.ReplacePbr(p => Pbr(p, baseColorB: v)),
            ValidateUnitFloat);
        Register(byId, BaseColorA, unitFloat with { Renderer = "color-rgba" }, "material.base_color.a",
            static state => state.Source.PbrMetallicRoughness.BaseColorA,
            static (state, v) => state.ReplacePbr(p => Pbr(p, baseColorA: v)),
            ValidateUnitFloat);
        Register(byId, Metalness, unitFloat, "material.metalness",
            static state => state.Source.PbrMetallicRoughness.MetallicFactor,
            static (state, v) => state.ReplacePbr(p => Pbr(p, metallicFactor: v)),
            ValidateUnitFloat);
        Register(byId, Roughness, unitFloat, "material.roughness",
            static state => state.Source.PbrMetallicRoughness.RoughnessFactor,
            static (state, v) => state.ReplacePbr(p => Pbr(p, roughnessFactor: v)),
            ValidateUnitFloat);
        Register(byId, AlphaCutoff, unitFloat, "material.alpha_cutoff",
            static state => state.Source.AlphaCutoff,
            static (state, v) => state.Replace(s => WithAlphaCutoff(s, v)),
            ValidateUnitFloat);

        Register(byId, AmbientOcclusion, unitFloat, "material.ambient_occlusion",
            static state => state.Source.OcclusionTexture?.Strength ?? 1.0f,
            static (state, v) => state.Replace(s => WithOcclusionStrength(s, v)),
            ValidateUnitFloat);

        Register(byId, NormalScale,
            new EditorAnnotation { Group = "Parameters", Renderer = "numberbox", Step = 0.01, SoftMin = 0.0, SoftMax = 4.0 },
            "material.normal_scale",
            static state => state.Source.NormalTexture?.Scale ?? 1.0f,
            static (state, v) => state.Replace(s => WithNormalScale(s, v)),
            ValidateNonNegativeFloat);

        Register(byId, DoubleSided, new EditorAnnotation { Group = "Surface", Renderer = "checkbox" },
            "material.double_sided",
            static state => state.Source.DoubleSided,
            static (state, v) => state.Replace(s => WithDoubleSided(s, v)),
            static _ => ValidationResult.Ok);

        Register(byId, AlphaMode, new EditorAnnotation { Group = "Surface", Renderer = "enum-dropdown" },
            "material.alpha_mode",
            static state => state.Source.AlphaMode,
            static (state, v) => state.SetAlphaMode(v),
            static value => Enum.IsDefined(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail("PROPERTY_ENUM_INVALID", "Alpha mode is not supported."));

        return new MaterialDescriptors(byId);
    }

    private static void Register<T>(
        Dictionary<PropertyId, PropertyDescriptor> table,
        PropertyId<T> id,
        EditorAnnotation annotation,
        string engineKey,
        Func<MaterialEditState, T> read,
        Action<MaterialEditState, T> write,
        Func<T, ValidationResult> validator)
    {
        ArgumentNullException.ThrowIfNull(validator);

        var descriptor = new PropertyDescriptor<T>(
            id: id,
            reader: target => read((MaterialEditState)target),
            writer: (target, value) => write((MaterialEditState)target, value),
            validator: validator,
            annotation: annotation,
            engineCommandKey: engineKey);
        table[id.Id] = descriptor;
    }

    private static ValidationResult ValidateUnitFloat(float value)
    {
        if (!float.IsFinite(value))
        {
            return ValidationResult.Fail("PROPERTY_NONFINITE", "Value must be finite.");
        }

        return value is >= 0.0f and <= 1.0f
            ? ValidationResult.Ok
            : ValidationResult.Fail("PROPERTY_OUT_OF_RANGE", "Value must be between 0 and 1.");
    }

    private static ValidationResult ValidateNonNegativeFloat(float value)
    {
        if (!float.IsFinite(value))
        {
            return ValidationResult.Fail("PROPERTY_NONFINITE", "Value must be finite.");
        }

        return value >= 0.0f
            ? ValidationResult.Ok
            : ValidationResult.Fail("PROPERTY_OUT_OF_RANGE", "Value must be greater than or equal to 0.");
    }

    private static MaterialPbrMetallicRoughness Pbr(
        MaterialPbrMetallicRoughness source,
        float? baseColorR = null,
        float? baseColorG = null,
        float? baseColorB = null,
        float? baseColorA = null,
        float? metallicFactor = null,
        float? roughnessFactor = null) => new(
            baseColorR: baseColorR ?? source.BaseColorR,
            baseColorG: baseColorG ?? source.BaseColorG,
            baseColorB: baseColorB ?? source.BaseColorB,
            baseColorA: baseColorA ?? source.BaseColorA,
            metallicFactor: metallicFactor ?? source.MetallicFactor,
            roughnessFactor: roughnessFactor ?? source.RoughnessFactor,
            baseColorTexture: source.BaseColorTexture,
            metallicRoughnessTexture: source.MetallicRoughnessTexture);

    private static MaterialSource WithAlphaCutoff(MaterialSource source, float v) => new(
        schema: source.Schema,
        type: source.Type,
        name: source.Name,
        pbrMetallicRoughness: source.PbrMetallicRoughness,
        normalTexture: source.NormalTexture,
        occlusionTexture: source.OcclusionTexture,
        alphaMode: source.AlphaMode,
        alphaCutoff: v,
        doubleSided: source.DoubleSided);

    private static MaterialSource WithDoubleSided(MaterialSource source, bool v) => new(
        schema: source.Schema,
        type: source.Type,
        name: source.Name,
        pbrMetallicRoughness: source.PbrMetallicRoughness,
        normalTexture: source.NormalTexture,
        occlusionTexture: source.OcclusionTexture,
        alphaMode: source.AlphaMode,
        alphaCutoff: source.AlphaCutoff,
        doubleSided: v);

    private static MaterialSource WithNormalScale(MaterialSource source, float scale)
    {
        var normal = source.NormalTexture is { } n
            ? (NormalTextureRef?)(n with { Scale = scale })
            : null;
        return new MaterialSource(
            schema: source.Schema,
            type: source.Type,
            name: source.Name,
            pbrMetallicRoughness: source.PbrMetallicRoughness,
            normalTexture: normal,
            occlusionTexture: source.OcclusionTexture,
            alphaMode: source.AlphaMode,
            alphaCutoff: source.AlphaCutoff,
            doubleSided: source.DoubleSided);
    }

    private static MaterialSource WithOcclusionStrength(MaterialSource source, float strength)
    {
        var occlusion = source.OcclusionTexture is { } o
            ? (OcclusionTextureRef?)(o with { Strength = strength })
            : null;
        return new MaterialSource(
            schema: source.Schema,
            type: source.Type,
            name: source.Name,
            pbrMetallicRoughness: source.PbrMetallicRoughness,
            normalTexture: source.NormalTexture,
            occlusionTexture: occlusion,
            alphaMode: source.AlphaMode,
            alphaCutoff: source.AlphaCutoff,
            doubleSided: source.DoubleSided);
    }
}

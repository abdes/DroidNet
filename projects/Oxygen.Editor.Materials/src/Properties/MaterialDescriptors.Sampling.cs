// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Materials.Properties;

/// <summary>The descriptors of how a channel reads its texture, of the occlusion mode and of the procedural grid.</summary>
public sealed partial class MaterialDescriptors
{
    // Read when the catalog is built, whatever the order of this class's static initializers.
    private static MaterialProceduralGrid GridDefaults => MaterialProceduralGrid.EngineDefaults;

    /// <summary>Gets the properties of one texture channel.</summary>
    /// <param name="channel">The channel, one of <see cref="MaterialSource.TextureChannels"/>.</param>
    /// <returns>The channel's properties.</returns>
    public static MaterialSlotProperties Slot(string channel) => new(channel);

    private static void RegisterSampling(Dictionary<PropertyId, PropertyDescriptor> table)
    {
        foreach (var channel in MaterialSource.TextureChannels)
        {
            var slot = Slot(channel);
            RegisterBinding(table, channel, slot.UvSet, 0, static b => b.UvSet, static (b, v) => b with { UvSet = v }, ValidateUvSet);
            RegisterBinding(table, channel, slot.ScaleU, 1.0f, static b => b.UvTransform.Scale.X, static (b, v) => WithScale(b, new Vector2(v, b.UvTransform.Scale.Y)), ValidateFinite);
            RegisterBinding(table, channel, slot.ScaleV, 1.0f, static b => b.UvTransform.Scale.Y, static (b, v) => WithScale(b, new Vector2(b.UvTransform.Scale.X, v)), ValidateFinite);
            RegisterBinding(table, channel, slot.OffsetU, 0.0f, static b => b.UvTransform.Offset.X, static (b, v) => WithOffset(b, new Vector2(v, b.UvTransform.Offset.Y)), ValidateFinite);
            RegisterBinding(table, channel, slot.OffsetV, 0.0f, static b => b.UvTransform.Offset.Y, static (b, v) => WithOffset(b, new Vector2(b.UvTransform.Offset.X, v)), ValidateFinite);
            RegisterBinding(table, channel, slot.RotationRadians, 0.0f, static b => b.UvTransform.RotationRadians, static (b, v) => b with { UvTransform = b.UvTransform with { RotationRadians = v } }, ValidateFinite);
            RegisterBinding(table, channel, slot.AddressU, MaterialTextureAddress.Repeat, static b => b.Sampler.AddressU, static (b, v) => b with { Sampler = b.Sampler with { AddressU = v } }, ValidateDefined);
            RegisterBinding(table, channel, slot.AddressV, MaterialTextureAddress.Repeat, static b => b.Sampler.AddressV, static (b, v) => b with { Sampler = b.Sampler with { AddressV = v } }, ValidateDefined);
            RegisterBinding(table, channel, slot.MagFilter, MaterialTextureFilter.Linear, static b => b.Sampler.MagFilter, static (b, v) => b with { Sampler = b.Sampler with { MagFilter = v } }, ValidateDefined);
            RegisterBinding(table, channel, slot.MinFilter, MaterialTextureFilter.Linear, static b => b.Sampler.MinFilter, static (b, v) => b with { Sampler = b.Sampler with { MinFilter = v } }, ValidateDefined);
            RegisterBinding(table, channel, slot.MipFilter, MaterialTextureMipFilter.Linear, static b => b.Sampler.MipFilter, static (b, v) => b with { Sampler = b.Sampler with { MipFilter = v } }, ValidateDefined);
        }
    }

    // A value equal to the engine's default is not stated, so a descriptor never writes one.
    private static void RegisterGrid(Dictionary<PropertyId, PropertyDescriptor> table)
    {
        RegisterGridSpacing(table);
        RegisterGridValue(table, MaterialGridProperties.MajorEvery, GridDefaults.MajorEvery!.Value, static g => g.MajorEvery, static (g, v) => g with { MajorEvery = v }, ValidateMajorEvery);
        RegisterGridValue(table, MaterialGridProperties.LineThickness, GridDefaults.LineThickness!.Value, static g => g.LineThickness, static (g, v) => g with { LineThickness = v }, ValidateNonNegativeFloat);
        RegisterGridValue(table, MaterialGridProperties.MajorThickness, GridDefaults.MajorThickness!.Value, static g => g.MajorThickness, static (g, v) => g with { MajorThickness = v }, ValidateNonNegativeFloat);
        RegisterGridValue(table, MaterialGridProperties.AxisThickness, GridDefaults.AxisThickness!.Value, static g => g.AxisThickness, static (g, v) => g with { AxisThickness = v }, ValidateNonNegativeFloat);
        RegisterGridValue(table, MaterialGridProperties.FadeStart, GridDefaults.FadeStart!.Value, static g => g.FadeStart, static (g, v) => g with { FadeStart = v }, ValidateNonNegativeFloat);
        RegisterGridValue(table, MaterialGridProperties.FadeEnd, GridDefaults.FadeEnd!.Value, static g => g.FadeEnd, static (g, v) => g with { FadeEnd = v }, ValidateNonNegativeFloat);
        RegisterGridColor(table, MaterialGridProperties.MinorColor, GridDefaults.MinorColor!.Value, static g => g.MinorColor, static (g, v) => g with { MinorColor = v });
        RegisterGridColor(table, MaterialGridProperties.MajorColor, GridDefaults.MajorColor!.Value, static g => g.MajorColor, static (g, v) => g with { MajorColor = v });
        RegisterGridColor(table, MaterialGridProperties.AxisColorX, GridDefaults.AxisColorX!.Value, static g => g.AxisColorX, static (g, v) => g with { AxisColorX = v });
        RegisterGridColor(table, MaterialGridProperties.AxisColorY, GridDefaults.AxisColorY!.Value, static g => g.AxisColorY, static (g, v) => g with { AxisColorY = v });
        RegisterGridColor(table, MaterialGridProperties.OriginColor, GridDefaults.OriginColor!.Value, static g => g.OriginColor, static (g, v) => g with { OriginColor = v });
    }

    private static void RegisterGridSpacing(Dictionary<PropertyId, PropertyDescriptor> table)
    {
        var fallback = GridDefaults.Spacing!.Value;
        RegisterGridField(
            table,
            MaterialGridProperties.SpacingX,
            fallback.X,
            g => (g.Spacing ?? fallback).X,
            (g, v) => g with { Spacing = Stated(new Vector2(v, (g.Spacing ?? fallback).Y), fallback) },
            ValidatePositiveFloat);
        RegisterGridField(
            table,
            MaterialGridProperties.SpacingY,
            fallback.Y,
            g => (g.Spacing ?? fallback).Y,
            (g, v) => g with { Spacing = Stated(new Vector2((g.Spacing ?? fallback).X, v), fallback) },
            ValidatePositiveFloat);
    }

    private static void RegisterGridValue<T>(
        Dictionary<PropertyId, PropertyDescriptor> table,
        PropertyId<T> id,
        T fallback,
        Func<MaterialProceduralGrid, T?> read,
        Func<MaterialProceduralGrid, T?, MaterialProceduralGrid> write,
        Func<T, ValidationResult> validator)
        where T : struct, IEquatable<T>
        => RegisterGridField(table, id, fallback, g => read(g) ?? fallback, (g, v) => write(g, Stated(v, fallback)), validator);

    private static void RegisterGridColor(
        Dictionary<PropertyId, PropertyDescriptor> table,
        MaterialColorProperties color,
        Vector4 fallback,
        Func<MaterialProceduralGrid, Vector4?> read,
        Func<MaterialProceduralGrid, Vector4?, MaterialProceduralGrid> write)
    {
        for (var index = 0; index < color.Channels.Count; index++)
        {
            var channel = index;
            RegisterGridField(
                table,
                color.Channels[channel],
                fallback[channel],
                g => (read(g) ?? fallback)[channel],
                (g, v) => write(g, Stated(WithChannel(read(g) ?? fallback, channel, v), fallback)),
                ValidateUnitFloat);
        }
    }

    private static void RegisterGridField<T>(
        Dictionary<PropertyId, PropertyDescriptor> table,
        PropertyId<T> id,
        T fallback,
        Func<MaterialProceduralGrid, T> read,
        Func<MaterialProceduralGrid, T, MaterialProceduralGrid> write,
        Func<T, ValidationResult> validator)
        => Register(
            table,
            id,
            new EditorAnnotation { Group = "Surface", Renderer = "numberbox" },
            state => state.Source.ProceduralGrid is { } grid ? read(grid) : fallback,
            (state, value) => state.Replace(source => source.ProceduralGrid is { } grid ? source with { ProceduralGrid = write(grid, value) } : source),
            validator);

    private static void RegisterBinding<T>(
        Dictionary<PropertyId, PropertyDescriptor> table,
        string channel,
        PropertyId<T> id,
        T fallback,
        Func<MaterialTextureBinding, T> read,
        Func<MaterialTextureBinding, T, MaterialTextureBinding> write,
        Func<T, ValidationResult> validator)
        => Register(
            table,
            id,
            new EditorAnnotation { Group = "Textures", Renderer = "numberbox" },
            state => state.Source.TextureBindings.GetValueOrDefault(channel) is { } binding ? read(binding) : fallback,
            (state, value) => state.Replace(source => source.TextureBindings.GetValueOrDefault(channel) is { } binding ? source.WithTextureBinding(channel, write(binding, value)) : source),
            validator);

    private static T? Stated<T>(T value, T fallback)
        where T : struct, IEquatable<T>
        => value.Equals(fallback) ? null : value;

    private static Vector4 WithChannel(Vector4 color, int channel, float value)
    {
        color[channel] = value;
        return color;
    }

    private static MaterialTextureBinding WithScale(MaterialTextureBinding binding, Vector2 scale)
        => binding with { UvTransform = binding.UvTransform with { Scale = scale } };

    private static MaterialTextureBinding WithOffset(MaterialTextureBinding binding, Vector2 offset)
        => binding with { UvTransform = binding.UvTransform with { Offset = offset } };

    private static ValidationResult ValidateFinite(float value)
        => float.IsFinite(value) ? ValidationResult.Ok : ValidationResult.Fail(MaterialDiagnosticCodes.PropertyValueNotFinite, "Value must be finite.");

    private static ValidationResult ValidatePositiveFloat(float value)
    {
        if (!float.IsFinite(value))
        {
            return ValidationResult.Fail(MaterialDiagnosticCodes.PropertyValueNotFinite, "Value must be finite.");
        }

        return value > 0.0f
            ? ValidationResult.Ok
            : ValidationResult.Fail(MaterialDiagnosticCodes.PropertyValueOutOfRange, "Value must be greater than 0.");
    }

    private static ValidationResult ValidateUvSet(int value)
        => value is >= 0 and < MaterialTextureBinding.UvSetCount
            ? ValidationResult.Ok
            : ValidationResult.Fail(MaterialDiagnosticCodes.PropertyValueOutOfRange, "A vertex holds UV sets 0 and 1.");

    private static ValidationResult ValidateMajorEvery(uint value)
        => value >= 1
            ? ValidationResult.Ok
            : ValidationResult.Fail(MaterialDiagnosticCodes.PropertyValueOutOfRange, "A major line comes after at least one cell.");

    private static ValidationResult ValidateDefined<T>(T value)
        where T : struct, Enum
        => Enum.IsDefined(value)
            ? ValidationResult.Ok
            : ValidationResult.Fail(MaterialDiagnosticCodes.PropertyValueUndefined, "The value is not one the engine knows.");
}

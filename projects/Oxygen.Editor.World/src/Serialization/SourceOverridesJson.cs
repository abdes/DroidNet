// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization.Metadata;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Converts <see cref="SourceOverrides"/> and override values to and from their data: a value is read as the type
/// <see cref="SceneSchema.Overridable"/> gives its property.
/// </summary>
public static class SourceOverridesJson
{
    /// <summary>
    /// Reads the overrides of a component.
    /// </summary>
    /// <param name="componentKind">The component kind.</param>
    /// <param name="data">The data, or <see langword="null"/> when the component overrides nothing.</param>
    /// <returns>The overrides.</returns>
    /// <exception cref="JsonException">A key is not an overridable property of the component or part, or a value does not fit it.</exception>
    public static SourceOverrides Read(string componentKind, SourceOverridesData? data)
    {
        var overrides = SourceOverrides.For(componentKind);
        foreach (var (key, value) in data?.Values ?? new Dictionary<string, JsonElement>(StringComparer.Ordinal))
        {
            overrides = Add(overrides, new PropertyId(componentKind, key), part: null, layoutRevision: null, value);
        }

        foreach (var part in data?.Parts ?? [])
        {
            if (part.Values.Count == 0)
            {
                throw new JsonException($"The overrides of {part.Kind} {part.Id} hold no value.");
            }

            foreach (var (key, value) in part.Values)
            {
                overrides = Add(overrides, new PropertyId(componentKind, part.Kind, key), new PartId(part.Kind, part.Id), part.LayoutRevision, value);
            }
        }

        return overrides;
    }

    /// <summary>
    /// Writes the overrides of a component.
    /// </summary>
    /// <param name="overrides">The overrides.</param>
    /// <returns>The data, or <see langword="null"/> when nothing is overridden.</returns>
    public static SourceOverridesData? Write(SourceOverrides overrides)
    {
        if (overrides.IsEmpty)
        {
            return null;
        }

        var entries = overrides.All.ToList();
        var values = entries.Where(static entry => entry.Part is null).ToDictionary(static entry => entry.Property.JsonPointer, Encode, StringComparer.Ordinal);
        var parts = entries.Where(static entry => entry.Part is not null)
            .GroupBy(static entry => entry.Part!.Value)
            .Select(group => new PartOverridesData
            {
                Kind = group.Key.Kind,
                Id = group.Key.Id,
                LayoutRevision = group.First().LayoutRevision!,
                Values = group.ToDictionary(static entry => entry.Property.JsonPointer, Encode, StringComparer.Ordinal),
            })
            .ToList();
        return new SourceOverridesData
        {
            Values = values.Count == 0 ? null : values,
            Parts = parts.Count == 0 ? null : parts,
        };
    }

    /// <summary>
    /// Reads an override value as its property's type.
    /// </summary>
    /// <param name="property">The overridden property.</param>
    /// <param name="json">The value's JSON.</param>
    /// <returns>The value.</returns>
    /// <exception cref="JsonException">The property cannot be overridden, or the JSON is not a value of its type.</exception>
    public static object ReadValue(PropertyId property, JsonElement json)
    {
        ArgumentNullException.ThrowIfNull(property);
        if (!SceneSchema.Overridable.TryGetValue(property, out var type))
        {
            throw new JsonException($"{property} is not a property a source may supply.");
        }

        var value = json.Deserialize(TypeInfo(type)) ?? throw new JsonException($"The override of {property} has no value.");
        return value is LodPolicy policy && policy.FindProblem() is { } problem
            ? throw new JsonException($"The override of {property} is invalid: {problem}")
            : value;
    }

    /// <summary>
    /// Writes an override value as the JSON of its property's value type, the type <see cref="ReadValue"/> reads it as, so a
    /// polymorphic value such as a LOD policy keeps its type discriminator.
    /// </summary>
    /// <param name="property">The overridable property the value belongs to.</param>
    /// <param name="value">The value, of the property's type.</param>
    /// <returns>The value's JSON.</returns>
    public static JsonElement WriteValue(PropertyId property, object value)
    {
        ArgumentNullException.ThrowIfNull(property);
        ArgumentNullException.ThrowIfNull(value);
        return SceneSchema.Overridable.TryGetValue(property, out var type)
            ? JsonSerializer.SerializeToElement(value, TypeInfo(type))
            : throw new ArgumentException($"{property} is not a property a source may supply.", nameof(property));
    }

    private static SourceOverrides Add(SourceOverrides overrides, PropertyId property, PartId? part, string? layoutRevision, JsonElement json)
    {
        try
        {
            return overrides.With(new SourceOverride(property, part, layoutRevision, ReadValue(property, json)));
        }
        catch (ArgumentException exception)
        {
            throw new JsonException(exception.Message, exception);
        }
    }

    private static JsonElement Encode(SourceOverride entry) => WriteValue(entry.Property, entry.Value);

    // Every overridable value type needs a contract in the scene context; a missing one is a defect, not bad data.
    private static JsonTypeInfo TypeInfo(Type type)
        => SceneJsonContext.Default.GetTypeInfo(type)
            ?? throw new InvalidOperationException($"{nameof(SceneJsonContext)} has no contract for {type.Name}.");
}

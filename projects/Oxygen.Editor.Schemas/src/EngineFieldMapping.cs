// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using System.Numerics;

namespace Oxygen.Editor.Schemas;

/// <summary>Where a property's value goes in the engine: a component and one field per encoded value.</summary>
/// <remarks>
/// The engine takes every property as floats. A scalar maps to one field; a vector maps to one field per axis. The
/// default encoding writes floats and integers as they are, booleans as one or zero and enumerations as their numeric
/// value; <see cref="Encode"/> replaces it when the engine expects another unit.
/// </remarks>
/// <param name="Component">The engine component, as the engine numbers it.</param>
/// <param name="Fields">The engine fields, one per encoded value.</param>
public sealed record EngineFieldMapping(ushort Component, ImmutableArray<ushort> Fields)
{
    /// <summary>Gets the encoding of a value into one float per field, or <see langword="null"/> for the default.</summary>
    public Func<object?, ImmutableArray<float>>? Encode { get; init; }

    /// <summary>Creates a mapping with the default encoding.</summary>
    /// <param name="component">The engine component.</param>
    /// <param name="fields">The engine fields, one per encoded value.</param>
    /// <returns>The mapping.</returns>
    public static EngineFieldMapping Of(ushort component, params ReadOnlySpan<ushort> fields) => new(component, [.. fields]);

    /// <summary>Encodes a value into one float per field.</summary>
    /// <param name="value">The property's value.</param>
    /// <returns>The encoded values, in field order.</returns>
    /// <exception cref="InvalidOperationException">The value does not encode into one float per field.</exception>
    public ImmutableArray<float> EncodeValue(object? value)
    {
        var encoded = this.Encode?.Invoke(value) ?? EncodeDefault(value);
        return encoded.Length == this.Fields.Length
            ? encoded
            : throw new InvalidOperationException($"A value of type {value?.GetType().Name ?? "null"} encodes into {encoded.Length} floats, not {this.Fields.Length}.");
    }

    private static ImmutableArray<float> EncodeDefault(object? value)
        => value switch
        {
            float scalar => [scalar],
            int integer => [integer],
            bool flag => [flag ? 1f : 0f],
            Enum enumeration => [Convert.ToInt32(enumeration, CultureInfo.InvariantCulture)],
            Vector3 vector => [vector.X, vector.Y, vector.Z],
            Vector4 vector => [vector.X, vector.Y, vector.Z, vector.W],
            _ => throw new InvalidOperationException($"A value of type {value?.GetType().Name ?? "null"} has no default engine encoding."),
        };
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Factory helpers for optional partial-edit values.
/// </summary>
public static class OptionalEditValues
{
    /// <summary>
    /// Creates an unspecified optional field.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <returns>The optional wrapper.</returns>
    public static OptionalEditValue<T> Unspecified<T>() => default;

    /// <summary>
    /// Creates an optional field with an explicitly supplied value.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    /// <param name="value">The supplied value.</param>
    /// <returns>The optional wrapper.</returns>
    public static OptionalEditValue<T> Supplied<T>(T? value) => new(value);
}

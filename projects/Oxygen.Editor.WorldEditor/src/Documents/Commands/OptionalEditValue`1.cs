// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Represents an explicitly supplied partial-edit value.
/// </summary>
/// <typeparam name="T">The value type.</typeparam>
[StructLayout(LayoutKind.Auto)]
public readonly record struct OptionalEditValue<T>
{
    private readonly T? value;

    /// <summary>
    /// Initializes a new instance of the <see cref="OptionalEditValue{T}"/> struct with a supplied value.
    /// </summary>
    /// <param name="value">The supplied value.</param>
    internal OptionalEditValue(T? value)
    {
        this.HasValue = true;
        this.value = value;
    }

    /// <summary>
    /// Gets a value indicating whether the edit supplied this field.
    /// </summary>
    public bool HasValue { get; }

    /// <summary>
    /// Gets the supplied value.
    /// </summary>
    public T? Value
        => this.HasValue
            ? this.value
            : throw new InvalidOperationException("Optional value was not supplied.");

    /// <summary>
    /// Deconstructs this instance into its presence flag and value.
    /// </summary>
    /// <param name="hasValue">Whether the value was supplied.</param>
    /// <param name="value">The supplied value.</param>
    public void Deconstruct(out bool hasValue, out T? value)
    {
        hasValue = this.HasValue;
        value = this.value;
    }
}

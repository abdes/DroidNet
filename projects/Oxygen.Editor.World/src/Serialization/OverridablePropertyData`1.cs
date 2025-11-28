// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for serializing <see cref="World.OverridableProperty{T}"/>.
/// </summary>
/// <typeparam name="T">The type of the property value.</typeparam>
/// <remarks>
/// This DTO stores only the override state, not the default value.
/// The default value is restored from the slot's initialization when deserializing.
/// </remarks>
public record OverridablePropertyData<T>
{
    /// <summary>
    /// Gets or initializes the override value.
    /// </summary>
    public T? OverrideValue { get; init; }

    /// <summary>
    /// Gets a value indicating whether gets or initializes a value indicating whether the property is overridden.
    /// </summary>
    public bool IsOverridden { get; init; }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Represents a property that can have a default value or an overridden value.
/// </summary>
/// <typeparam name="T">The type of the property value.</typeparam>
/// <remarks>
/// This is a lightweight value type (struct) designed to represent properties that support
/// override semantics without observability overhead. The parent container (typically an
/// <see cref="Slots.OverrideSlot"/>) raises PropertyChanged events when this struct is replaced.
/// </remarks>
[StructLayout(LayoutKind.Auto)]
public readonly record struct OverridableProperty<T>(T DefaultValue, T? OverrideValue, bool IsOverridden)
{
    /// <summary>
    /// Gets the effective value, which is the override value if overridden, otherwise the default value.
    /// </summary>
    public T EffectiveValue => this.IsOverridden ? this.OverrideValue! : this.DefaultValue;

    /// <summary>
    /// Creates a new overridable property with an override value.
    /// </summary>
    /// <param name="value">The override value.</param>
    /// <returns>A new overridable property with the override applied.</returns>
    public OverridableProperty<T> WithOverride(T value)
        => new(this.DefaultValue, value, IsOverridden: true);

    /// <summary>
    /// Creates a new overridable property with the override cleared.
    /// </summary>
    /// <returns>A new overridable property without an override.</returns>
    public OverridableProperty<T> ClearOverride()
        => new(this.DefaultValue, default, IsOverridden: false);

    /// <summary>
    /// Converts this overridable property to its DTO representation.
    /// </summary>
    /// <returns>
    /// An <see cref="OverridablePropertyData{T}"/> when overridden, or <see langword="null"/> when not overridden.
    /// </returns>
    public OverridablePropertyData<T>? ToDto()
        => this.IsOverridden
            ? new OverridablePropertyData<T> { OverrideValue = this.OverrideValue, IsOverridden = true }
            : null;
}

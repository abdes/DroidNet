// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Non-generic helper providing factory and DTO mapping helpers for <see cref="OverridableProperty{T}"/>.
/// </summary>
public static class OverridableProperty
{
    /// <summary>
    /// Creates an overridable property with only a default value (not overridden).
    /// </summary>
    /// <typeparam name="T">The type of the property value.</typeparam>
    /// <param name="defaultValue">The default value the property uses when not overridden.</param>
    /// <returns>An <see cref="OverridableProperty{T}"/> with <see cref="OverridableProperty{T}.IsOverridden"/> set to <see langword="false"/>.</returns>
    public static OverridableProperty<T> FromDefault<T>(T defaultValue)
        => new(defaultValue, default, IsOverridden: false);

    /// <summary>
    /// Creates an <see cref="OverridableProperty{T}"/> from DTO data.
    /// </summary>
    /// <typeparam name="T">The type of the property value.</typeparam>
    /// <param name="defaultValue">The default value the property uses when not overridden.</param>
    /// <param name="dto">The DTO describing override state (may be <see langword="null"/>).</param>
    /// <returns>
    /// A new <see cref="OverridableProperty{T}"/> initialized from the DTO and default value. If the DTO is null or indicates no override, the returned value will have <see cref="OverridableProperty{T}.IsOverridden"/> = <see langword="false"/>.
    /// </returns>
    public static OverridableProperty<T> FromDto<T>(T defaultValue, OverridablePropertyData<T>? dto)
        => dto is { IsOverridden: true }
            ? new(defaultValue, dto.OverrideValue, IsOverridden: true)
            : FromDefault(defaultValue);
}

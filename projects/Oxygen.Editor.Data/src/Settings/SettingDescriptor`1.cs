// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Describes metadata and validation information for a single typed setting.
/// Provides metadata that can be used by UI editors and validation routines.
/// </summary>
/// <typeparam name="T">The type of the setting value.</typeparam>
public sealed class SettingDescriptor<T> : ISettingDescriptor
{
    /// <summary>
    /// Gets the typed setting key for this descriptor.
    /// </summary>
    public required SettingKey<T> Key { get; init; }

    /// <summary>
    /// Gets the display name for the setting when presented in UI.
    /// </summary>
    public string? DisplayName { get; init; }

    /// <summary>
    /// Gets the description or help text for the setting.
    /// </summary>
    public string? Description { get; init; }

    /// <summary>
    /// Gets the category to which the setting belongs.
    /// </summary>
    public string? Category { get; init; }

    /// <summary>
    /// Gets any validation attributes associated with the setting.
    /// </summary>
    public IReadOnlyList<ValidationAttribute> Validators { get; init; } = [];

    /// <summary>
    /// Gets the module name for the setting.
    /// </summary>
    string ISettingDescriptor.SettingsModule => this.Key.SettingsModule;

    /// <summary>
    /// Gets the name of the setting.
    /// </summary>
    string ISettingDescriptor.Name => this.Key.Name;

    /// <summary>
    /// Gets the display name for the setting.
    /// </summary>
    string? ISettingDescriptor.DisplayName => this.DisplayName;

    /// <summary>
    /// Gets the description for the setting.
    /// </summary>
    string? ISettingDescriptor.Description => this.Description;

    /// <summary>
    /// Gets the category for the setting.
    /// </summary>
    string? ISettingDescriptor.Category => this.Category;
}

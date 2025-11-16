// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Non-generic descriptor interface used for discovery APIs.
/// </summary>
public interface ISettingDescriptor
{
    /// <summary>
    /// Gets the module name for the setting.
    /// This is the same value as <see cref="SettingKey{T}.SettingsModule"/> and corresponds to the <c>SettingsModule</c> column in the database.
    /// </summary>
    public string SettingsModule { get; }

    /// <summary>
    /// Gets name of the setting within the module.
    /// </summary>
    public string Name { get; }

    /// <summary>
    /// Gets optional display name.
    /// </summary>
    public string? DisplayName { get; }

    /// <summary>
    /// Gets optional description.
    /// </summary>
    public string? Description { get; }

    /// <summary>
    /// Gets optional category used for grouping in UI.
    /// </summary>
    public string? Category { get; }
}

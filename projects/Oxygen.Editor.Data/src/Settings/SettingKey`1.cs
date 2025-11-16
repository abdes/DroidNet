// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// A typed setting key identifying a module and a named setting within that module.
/// </summary>
/// <typeparam name="T">The value type for the setting.</typeparam>
public readonly record struct SettingKey<T>
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingKey{T}"/> struct.
    /// Initializes a new instance of <see cref="SettingKey{T}"/>.
    /// </summary>
    /// <param name="settingsModule">Name of the setting module the setting belongs to.</param>
    /// <param name="name">Name of the setting value.</param>
    public SettingKey(string settingsModule, string name)
    {
        this.SettingsModule = settingsModule ?? throw new ArgumentNullException(nameof(settingsModule));
        this.Name = name ?? throw new ArgumentNullException(nameof(name));
    }

    /// <summary>
    /// Gets the module name for this key.
    /// </summary>
    public string SettingsModule { get; }

    /// <summary>
    /// Gets the name of the setting within the module.
    /// </summary>
    public string Name { get; }

    /// <summary>
    /// Converts the key into a string path of the form 'Module/Name'.
    /// </summary>
    /// <returns>A string representation of the key.</returns>
    public string ToPath() => $"{this.SettingsModule}/{this.Name}";

    /// <summary>
    /// Converts the key to a path string.
    /// </summary>
    /// <returns>The string path representation of the key.</returns>
    public override string ToString() => this.ToPath();
}

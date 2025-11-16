// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Static helper methods for working with <see cref="SettingKey{T}"/> instances.
/// </summary>
public static class SettingKeys
{
    /// <summary>
    /// Parses a path string of the form 'Module/Name' into a typed <see cref="SettingKey{T}"/>.
    /// </summary>
    /// <returns>
    /// A <see cref="SettingKey{T}"/> instance constructed from the specified path.
    /// </returns>
    /// <param name="path">The setting key path in the format 'Module/Name'.</param>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    public static SettingKey<T> Parse<T>(string path)
    {
        var parts = path.Split('/', 2);
        if (parts.Length != 2)
        {
            throw new FormatException($"Invalid setting key: '{path}'. Expected format: Module/Name");
        }

        return new SettingKey<T>(parts[0], parts[1]);
    }
}

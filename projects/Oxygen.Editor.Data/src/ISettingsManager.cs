// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data;

/// <summary>
/// Provides methods for managing settings in the Oxygen Editor.
/// </summary>
public interface ISettingsManager
{
    /// <summary>
    /// Saves a setting for a specific module.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="moduleName">The name of the module.</param>
    /// <param name="key">The key for the setting.</param>
    /// <param name="value">The value of the setting.</param>
    /// <returns>A task that represents the asynchronous save operation.</returns>
    /// <remarks>
    /// This method serializes the setting value to JSON and stores it in the database. It also updates the cache
    /// and notifies any registered change handlers.
    /// </remarks>
    public Task SaveSettingAsync<T>(string moduleName, string key, T value);

    /// <summary>
    /// Loads a setting for a specific module.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="moduleName">The name of the module.</param>
    /// <param name="key">The key for the setting.</param>
    /// <returns>A task that represents the asynchronous load operation. The task result contains the setting value, or <see langword="null"/> if the setting does not exist.</returns>
    /// <remarks>
    /// This method retrieves the setting value from the cache if available. Otherwise, it loads the value from the database
    /// and updates the cache.
    /// </remarks>
    public Task<T?> LoadSettingAsync<T>(string moduleName, string key);

    /// <summary>
    /// Loads a setting for a specific module, returning a default value if the setting does not exist.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="moduleName">The name of the module.</param>
    /// <param name="key">The key for the setting.</param>
    /// <param name="defaultValue">The default value to return if the setting does not exist.</param>
    /// <returns>A task that represents the asynchronous load operation. The task result contains the setting value, or the default value if the setting does not exist.</returns>
    /// <remarks>
    /// This method is similar to <see cref="LoadSettingAsync{T}(string,string)"/>, but it returns a specified default value if the setting does not exist.
    /// </remarks>
    public Task<T> LoadSettingAsync<T>(string moduleName, string key, T defaultValue);

    /// <summary>
    /// Registers a change handler for a specific setting.
    /// </summary>
    /// <param name="moduleName">The name of the module.</param>
    /// <param name="key">The key for the setting.</param>
    /// <param name="handler">The handler to be invoked when the setting changes.</param>
    /// <remarks>
    /// The change handler is invoked with the new value of the setting whenever it is updated.
    /// </remarks>
    public void RegisterChangeHandler(string moduleName, string key, Action<string> handler);
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data;

/// <summary>
/// Manages the persistence and retrieval of module settings using a database context.
/// </summary>
/// <remarks>
/// The <see cref="EditorSettingsManager"/> class provides methods to save and load settings for different modules.
/// It uses a <see cref="PersistentState"/> context to interact with the database and caches settings in memory
/// to improve performance. The settings are serialized and deserialized using JSON.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <![CDATA[
/// // Initialize the PersistentState context
/// var options = new DbContextOptionsBuilder<PersistentState>()
///     .UseSqlite("Data Source=app.db")
///     .Options;
/// var context = new PersistentState(options);
///
/// // Create the EditorSettingsManager
/// var settingsManager = new EditorSettingsManager(context);
///
/// // Save a setting
/// await settingsManager.SaveSettingAsync("MyModule", "WindowPosition", new Point(100, 100));
///
/// // Load a setting
/// var windowPosition = await settingsManager.LoadSettingAsync<Point>("MyModule", "WindowPosition");
/// ]]>
/// </example>
public class EditorSettingsManager(PersistentState context) : IEditorSettingsManager
{
    private static readonly JsonSerializerOptions JsonSerializerOptions = new()
    {
        WriteIndented = false,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    private readonly ConcurrentDictionary<string, object?> cache = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, Action<string>> changeHandlers = new(StringComparer.Ordinal);

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
    public async Task SaveSettingAsync<T>(string moduleName, string key, T value)
    {
        var jsonValue = JsonSerializer.Serialize(value, JsonSerializerOptions);
        var cacheKey = $"{moduleName}:{key}";

        var setting = await context.Settings
            .FirstOrDefaultAsync(ms => ms.ModuleName == moduleName && ms.Key == key).ConfigureAwait(true);

        if (setting == null)
        {
            setting = new ModuleSetting
            {
                ModuleName = moduleName,
                Key = key,
                JsonValue = jsonValue,
                CreatedAt = DateTime.UtcNow,
                UpdatedAt = DateTime.UtcNow,
            };
            _ = context.Settings.Add(setting);
        }
        else
        {
            setting.JsonValue = jsonValue;
            setting.UpdatedAt = DateTime.UtcNow;
            _ = context.Settings.Update(setting);
        }

        _ = await context.SaveChangesAsync().ConfigureAwait(true);

        this.cache[cacheKey] = value;
        this.NotifyChange(cacheKey, jsonValue);
    }

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
    public async Task<T?> LoadSettingAsync<T>(string moduleName, string key)
    {
        var cacheKey = $"{moduleName}:{key}";
        if (this.cache.TryGetValue(cacheKey, out var cachedValue))
        {
            return (T?)cachedValue;
        }

        var setting = await context.Settings
            .FirstOrDefaultAsync(ms => ms.ModuleName == moduleName && ms.Key == key).ConfigureAwait(true);

        if (setting is null)
        {
            return default;
        }

        var value = setting.JsonValue is null ? default : JsonSerializer.Deserialize<T>(setting.JsonValue, JsonSerializerOptions);
        this.cache[cacheKey] = value;
        return value;
    }

    /// <summary>
    /// Loads a setting for a specific module, returning a default value if the setting does not exist.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="moduleName">The name of the module.</param>
    /// <param name="key">The key for the setting.</param>
    /// <param name="defaultValue">The default value to return if the setting does not exist.</param>
    /// <returns>A task that represents the asynchronous load operation. The task result contains the setting value, or the default value if the setting does not exist.</returns>
    /// <remarks>
    /// This method is similar to <see cref="LoadSettingAsync{T}(string, string)"/>, but it returns a specified default value if the setting does not exist.
    /// </remarks>
    public async Task<T> LoadSettingAsync<T>(string moduleName, string key, T defaultValue)
    {
        var value = await this.LoadSettingAsync<T>(moduleName, key).ConfigureAwait(true);
        return value is null ? defaultValue : value;
    }

    /// <summary>
    /// Registers a change handler for a specific setting.
    /// </summary>
    /// <param name="moduleName">The name of the module.</param>
    /// <param name="key">The key for the setting.</param>
    /// <param name="handler">The handler to be invoked when the setting changes.</param>
    /// <remarks>
    /// The change handler is invoked with the new value of the setting whenever it is updated.
    /// </remarks>
    public void RegisterChangeHandler(string moduleName, string key, Action<string> handler)
    {
        var cacheKey = $"{moduleName}:{key}";
        this.changeHandlers[cacheKey] = handler;
    }

    /// <summary>
    /// Clears the cache of settings.
    /// </summary>
    /// <remarks>
    /// This method clears all cached settings, forcing subsequent load operations to retrieve values from the database.
    /// </remarks>
    internal void ClearCache() => this.cache.Clear();

    /// <summary>
    /// Notifies registered change handlers of a setting change.
    /// </summary>
    /// <param name="cacheKey">The cache key for the setting.</param>
    /// <param name="newValue">The new value of the setting.</param>
    /// <remarks>
    /// This method is called internally whenever a setting is updated to notify any registered change handlers.
    /// </remarks>
    private void NotifyChange(string cacheKey, string newValue)
    {
        if (this.changeHandlers.TryGetValue(cacheKey, out var handler))
        {
            handler(newValue);
        }
    }
}

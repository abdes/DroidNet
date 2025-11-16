// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Provides methods for managing settings in the Oxygen Editor.
/// </summary>
public interface IEditorSettingsManager
{
    /// <summary>
    /// Saves a typed setting for a specific key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key containing SettingsModule and Name.</param>
    /// <param name="value">The value of the setting.</param>
    /// <param name="settingContext">Optional <see cref="SettingContext"/> for scope resolution.</param>
    /// <param name="progress">Optional progress reporter for save operations.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task completing when the value has been persisted.</returns>
    public Task SaveSettingAsync<T>(SettingKey<T> key, T value, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default);

    /// <summary>
    /// Loads a typed setting for a specific key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key containing SettingsModule and Name.</param>
    /// <param name="settingContext">Optional scope for load; if null, Application scope will be used.</param>
    /// <param name="progress">Optional progress reporter for load operations.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The setting value or <see langword="null"/> if not found.</returns>
    public Task<T?> LoadSettingAsync<T>(SettingKey<T> key, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default);

    /// <summary>
    /// Loads a typed setting for a specific key and returns the provided default value if it does not exist.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key containing SettingsModule and Name.</param>
    /// <param name="defaultValue">The default value to return if the setting does not exist.</param>
    /// <returns>The loaded value or the supplied default if no stored value exists.</returns>
    /// <param name="settingContext">Optional scope for load.</param>
    /// <param name="progress">Optional progress reporter for load operations.</param>
    /// <param name="ct">Cancellation token.</param>
    public Task<T> LoadSettingOrDefaultAsync<T>(SettingKey<T> key, T defaultValue, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default);

    /// <summary>
    /// Returns a typed observable that emits events whenever the specific setting changes.
    /// </summary>
    /// <typeparam name="T">The expected type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <returns>An <see cref="IObservable{T}"/> that emits setting change events for the specified key.</returns>
    public IObservable<SettingChangedEvent<T>> WhenSettingChanged<T>(SettingKey<T> key);

    /// <summary>
    /// Returns the scopes defined for the given setting key, optionally limited to a specific project ID.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">The typed setting key to query for definitions.</param>
    /// <param name="projectId">Optional project id to filter project scope entries.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A list of scopes where the setting is defined.</returns>
    public Task<IReadOnlyList<SettingScope>> GetDefinedScopesAsync<T>(SettingKey<T> key, string? projectId = null, CancellationToken ct = default);

    /// <summary>
    /// Get descriptors grouped by category for all known settings descriptor sets.
    /// </summary>
    /// <returns>A dictionary mapping category names to lists of descriptors.</returns>
    public IReadOnlyDictionary<string, IReadOnlyList<ISettingDescriptor>> GetDescriptorsByCategory();

    /// <summary>
    /// Search descriptors by free-text query matching key, display name, description or category.
    /// </summary>
    /// <param name="searchTerm">The search term to match against descriptors.</param>
    /// <returns>List of matching descriptors.</returns>
    public IReadOnlyList<ISettingDescriptor> SearchDescriptors(string searchTerm);

    /// <summary>
    /// Returns a list of known keys persisted in the database in the form 'Module/Name'.
    /// </summary>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>List of keys.</returns>
    public Task<IReadOnlyList<string>> GetAllKeysAsync(CancellationToken ct = default);

    /// <summary>
    /// Returns all persisted values for a specific key across all scopes.
    /// </summary>
    /// <param name="key">The key to query in the form 'Module/Name'.</param>
    /// <param name="progress">Optional progress reporter for the load operation.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>List of tuples containing Scope, ScopeId and value as raw object.</returns>
    public Task<IReadOnlyList<(SettingScope scope, string? scopeId, object? value)>> GetAllValuesAsync(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default);

    /// <summary>
    /// Returns all persisted values for a specific key across all scopes deserialized into <typeparamref name="T"/>.
    /// </summary>
    /// <typeparam name="T">The type to deserialize values to.</typeparam>
    /// <param name="key">The key to query in the form 'Module/Name'.</param>
    /// <param name="progress">Optional progress reporter for the typed load operation.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>List of tuples containing Scope, ScopeId and typed value.</returns>
    public Task<IReadOnlyList<(SettingScope scope, string? scopeId, T? value)>> GetAllValuesAsync<T>(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default);

    /// <summary>
    /// Attempts to get values for a key deserialized to <typeparamref name="T"/>.
    /// Returns a result with success flag and error messages for failed conversions.
    /// </summary>
    /// <typeparam name="T">The type to deserialize the persisted values to.</typeparam>
    /// <param name="key">The key to query in the form 'Module/Name'.</param>
    /// <param name="progress">Optional progress reporter for the try-get operation.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A typed result with details on success, values and any errors.</returns>
    public Task<TryGetAllValuesResult<T>> TryGetAllValuesAsync<T>(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default);

    /// <summary>
    /// Returns the timestamp when a setting was last updated for a specific key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key containing SettingsModule and Name.</param>
    /// <param name="settingContext">Optional scope for query; application scope will be used if null.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The <see cref="DateTime"/> when the setting was last updated or <see langword="null"/> if it doesn't exist.</returns>
    public Task<DateTime?> GetLastUpdatedTimeAsync<T>(SettingKey<T> key, SettingContext? settingContext = null, CancellationToken ct = default);
}

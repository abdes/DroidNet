// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Manages in-memory caching of setting values to improve performance.
/// </summary>
internal sealed class SettingsCache
{
    private readonly ConcurrentDictionary<string, object?> cache = new(StringComparer.Ordinal);

    /// <summary>
    /// Generates a cache key from setting components.
    /// </summary>
    /// <param name="settingsModule">The settings module name.</param>
    /// <param name="name">The setting name.</param>
    /// <param name="scope">The setting scope.</param>
    /// <param name="scopeId">Optional scope identifier.</param>
    /// <returns>A unique cache key string.</returns>
    public static string GenerateCacheKey(string settingsModule, string name, SettingScope scope, string? scopeId)
        => $"{settingsModule}:{name}:{(int)scope}:{scopeId}";

    /// <summary>
    /// Attempts to retrieve a cached value.
    /// </summary>
    /// <typeparam name="T">The type of the cached value.</typeparam>
    /// <param name="cacheKey">The cache key.</param>
    /// <param name="value">The cached value if found.</param>
    /// <returns>True if the value was found in cache; otherwise false.</returns>
    public bool TryGet<T>(string cacheKey, out T? value)
    {
        if (this.cache.TryGetValue(cacheKey, out var cachedValue))
        {
            value = (T?)cachedValue;
            return true;
        }

        value = default;
        return false;
    }

    /// <summary>
    /// Stores a value in the cache.
    /// </summary>
    /// <typeparam name="T">The type of the value.</typeparam>
    /// <param name="cacheKey">The cache key.</param>
    /// <param name="value">The value to cache.</param>
    public void Set<T>(string cacheKey, T? value)
    {
        this.cache[cacheKey] = value;
    }

    /// <summary>
    /// Clears all cached values.
    /// </summary>
    public void Clear()
    {
        this.cache.Clear();
    }

    /// <summary>
    /// Removes the cache value for the specified key.
    /// </summary>
    /// <param name="cacheKey">The cache key to remove.</param>
    public void Remove(string cacheKey) => _ = this.cache.TryRemove(cacheKey, out _);
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

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
/// await settingsManager.SaveSettingAsync(new Oxygen.Editor.Data.Settings.SettingKey<Point>("MyModule", "WindowPosition"), new Point(100, 100));
///
/// // Load a setting
/// var windowPosition = await settingsManager.LoadSettingOrDefaultAsync<Point>(new Oxygen.Editor.Data.Settings.SettingKey<Point>("MyModule", "WindowPosition"));
/// ]]>
/// </example>
public partial class EditorSettingsManager(PersistentState context, IDescriptorProvider? descriptorProvider = null) : IEditorSettingsManager
{
    private static readonly JsonSerializerOptions JsonSerializerOptions = new()
    {
        WriteIndented = false,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    private static readonly Lazy<StaticDescriptorProvider> LazyStaticProvider = new(
        () => new StaticDescriptorProvider(),
        LazyThreadSafetyMode.ExecutionAndPublication);

    private readonly PersistentState context = context;
    private readonly SettingsCache cache = new();
    private readonly SettingsNotifier notifier = new();
    private readonly IDescriptorProvider descriptorProvider = descriptorProvider ?? StaticProvider;

    /// <summary>
    /// Gets the global static descriptor provider for source-generated registration.
    /// </summary>
    /// <remarks>
    /// This provider is lazily initialized in a thread-safe manner to ensure it's available
    /// before any module initializers attempt to register descriptors.
    /// </remarks>
    public static StaticDescriptorProvider StaticProvider => LazyStaticProvider.Value;

    /// <summary>
    /// Saves a typed setting value for the provided key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="value">The value to save.</param>
    /// <param name="settingContext">Optional scope context.</param>
    /// <param name="progress">Optional progress reporter for save operations.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task that completes once the value has been persisted.</returns>
    public async Task SaveSettingAsync<T>(SettingKey<T> key, T value, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var jsonValue = JsonSerializer.Serialize(value, JsonSerializerOptions);
        var scope = settingContext?.Scope ?? SettingScope.Application;
        var scopeId = settingContext?.ScopeId;
        var cacheKey = SettingsCache.GenerateCacheKey(key.SettingsModule, key.Name, scope, scopeId);

        var setting = await this.FindSettingAsync(key.SettingsModule, key.Name, scope, scopeId, ct).ConfigureAwait(false);

        if (setting == null)
        {
            setting = new ModuleSetting
            {
                SettingsModule = key.SettingsModule,
                Name = key.Name,
                Scope = scope,
                ScopeId = scopeId,
                JsonValue = jsonValue,
                CreatedAt = DateTime.UtcNow,
                UpdatedAt = DateTime.UtcNow,
            };
            _ = this.context.Settings.Add(setting);
        }
        else
        {
            setting.JsonValue = jsonValue;
            setting.UpdatedAt = DateTime.UtcNow;
            _ = this.context.Settings.Update(setting);
        }

        _ = await this.context.SaveChangesAsync(ct).ConfigureAwait(false);

        // Report a single-save operation as a 1-of-1 save progress.
        progress?.Report(new SettingsProgress(1, 1, key.SettingsModule, key.Name));

        this.cache.Set(cacheKey, value);
        this.notifier.NotifyChange(key.SettingsModule, key.Name, key, default, value, scope, scopeId);
    }

    /// <summary>
    /// Loads a typed setting value for the provided key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="settingContext">Optional scope context.</param>
    /// <param name="progress">Optional progress reporter for load operations.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The deserialized value or <see langword="null"/> if not present.</returns>
    public async Task<T?> LoadSettingAsync<T>(SettingKey<T> key, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        var scope = settingContext?.Scope ?? SettingScope.Application;
        var scopeId = settingContext?.ScopeId;
        var cacheKey = SettingsCache.GenerateCacheKey(key.SettingsModule, key.Name, scope, scopeId);

        if (this.cache.TryGet<T>(cacheKey, out var cachedValue))
        {
            return cachedValue;
        }

        var setting = await this.FindSettingAsync(key.SettingsModule, key.Name, scope, scopeId, ct).ConfigureAwait(false);

        if (setting is null)
        {
            // No persisted value, report zero total to indicate nothing loaded.
            progress?.Report(new SettingsProgress(0, 0, key.SettingsModule, key.Name));
            return default;
        }

        var value = setting.JsonValue is null ? default : JsonSerializer.Deserialize<T>(setting.JsonValue, JsonSerializerOptions);
        this.cache.Set(cacheKey, value);

        // Report single-setting load progress: 1 of 1
        progress?.Report(new SettingsProgress(1, 1, key.SettingsModule, key.Name));
        return value;
    }

    /// <summary>
    /// Loads a typed setting value for the provided key and returns a supplied default value if the setting does not exist.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="defaultValue">The default value to return if not present.</param>
    /// <param name="settingContext">Optional scope context.</param>
    /// <param name="progress">Optional progress reporter for load operations.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The deserialized value or the provided default when not present.</returns>
    public async Task<T> LoadSettingOrDefaultAsync<T>(SettingKey<T> key, T defaultValue, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        var value = await this.LoadSettingAsync(key, settingContext, progress: progress, ct: ct).ConfigureAwait(false);
        return value is null ? defaultValue : value;
    }

    /// <summary>
    /// Returns an observable for typed setting change notifications.
    /// </summary>
    /// <typeparam name="T">The type of the expected setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <returns>An observable emitting setting change events.</returns>
    public IObservable<SettingChangedEvent<T>> WhenSettingChanged<T>(SettingKey<T> key)
        => this.notifier.GetObservable<T>(key.SettingsModule, key.Name);

    /// <inheritdoc />
    public async Task<IReadOnlyList<SettingScope>> GetDefinedScopesAsync<T>(SettingKey<T> key, string? projectId = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var query = this.context.Settings.Where(ms => ms.SettingsModule == key.SettingsModule && ms.Name == key.Name);
        if (!string.IsNullOrEmpty(projectId))
        {
            query = query.Where(ms => ms.Scope == SettingScope.Application || (ms.Scope == SettingScope.Project && ms.ScopeId == projectId));
        }

        var scopes = await query.Select(ms => ms.Scope).Distinct().ToListAsync(ct).ConfigureAwait(false);
        return scopes;
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<string>> GetAllKeysAsync(CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        return await this.context.Settings.Select(s => s.SettingsModule + "/" + s.Name).Distinct().ToListAsync(ct).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<(SettingScope scope, string? scopeId, object? value)>> GetAllValuesAsync(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(key))
        {
            return [];
        }

        var parts = key.Split('/', 2);
        if (parts.Length != 2)
        {
            throw new FormatException($"Invalid setting key: '{key}'. Expected format: Module/Name");
        }

        var module = parts[0];
        var name = parts[1];
        var query = this.context.Settings.Where(ms => ms.SettingsModule == module && ms.Name == name);
        var results = await query.Select(ms => new { ms.Scope, ms.ScopeId, ms.JsonValue }).ToListAsync(ct).ConfigureAwait(false);
        var total = results.Count;
        var list = new List<(SettingScope, string?, object?)>(results.Count);
        for (var i = 0; i < results.Count; ++i)
        {
            var r = results[i];
            var val = r.JsonValue is null ? null : JsonSerializer.Deserialize<object>(r.JsonValue, JsonSerializerOptions);
            list.Add((r.Scope, r.ScopeId, val));
            progress?.Report(new SettingsProgress(total, i + 1, module, name));
        }

        return list;
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<(SettingScope scope, string? scopeId, T? value)>> GetAllValuesAsync<T>(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(key))
        {
            return [];
        }

        var parts = key.Split('/', 2);
        if (parts.Length != 2)
        {
            throw new FormatException($"Invalid setting key: '{key}'. Expected format: Module/Name");
        }

        var module = parts[0];
        var name = parts[1];
        var query = this.context.Settings.Where(ms => ms.SettingsModule == module && ms.Name == name);
        var results = await query.Select(ms => new { ms.Scope, ms.ScopeId, ms.JsonValue }).ToListAsync(ct).ConfigureAwait(false);
        var total = results.Count;
        var list = new List<(SettingScope, string?, T?)>(results.Count);
        for (var i = 0; i < results.Count; ++i)
        {
            var r = results[i];
            var val = r.JsonValue is null ? default : JsonSerializer.Deserialize<T>(r.JsonValue, JsonSerializerOptions);
            list.Add((r.Scope, r.ScopeId, val));
            progress?.Report(new SettingsProgress(total, i + 1, module, name));
        }

        return list;
    }

    /// <inheritdoc/>
    public async Task<TryGetAllValuesResult<T>> TryGetAllValuesAsync<T>(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var errors = new List<string>();
        var scopedValues = new List<ScopedValue<T>>();

        if (string.IsNullOrWhiteSpace(key))
        {
            errors.Add("Key is null or whitespace");
            return new TryGetAllValuesResult<T>(Success: false, scopedValues, errors);
        }

        var parts = key.Split('/', 2);
        if (parts.Length != 2)
        {
            errors.Add($"Invalid setting key: '{key}'. Expected format: Module/Name");
            return new TryGetAllValuesResult<T>(Success: false, scopedValues, errors);
        }

        var module = parts[0];
        var name = parts[1];
        var query = this.context.Settings.Where(ms => ms.SettingsModule == module && ms.Name == name);
        var results = await query.Select(ms => new { ms.Scope, ms.ScopeId, ms.JsonValue }).ToListAsync(ct).ConfigureAwait(false);
        var total = results.Count;
        for (var i = 0; i < results.Count; ++i)
        {
            var r = results[i];
            if (r.JsonValue is null)
            {
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
                progress?.Report(new SettingsProgress(total, i + 1, module, name));
                continue;
            }

            try
            {
                var val = JsonSerializer.Deserialize<T>(r.JsonValue, JsonSerializerOptions);
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, val));
                progress?.Report(new SettingsProgress(total, i + 1, module, name));
            }
            catch (System.Text.Json.JsonException ex)
            {
                errors.Add($"Failed to deserialize value for {key} at scope {r.Scope}{(r.ScopeId is null ? string.Empty : $"({r.ScopeId})")}: {ex.Message}");
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
                progress?.Report(new SettingsProgress(total, i + 1, module, name));
            }
            catch (NotSupportedException ex)
            {
                errors.Add($"Failed to deserialize value for {key} at scope {r.Scope}{(r.ScopeId is null ? string.Empty : $"({r.ScopeId})")}: {ex.Message}");
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
                progress?.Report(new SettingsProgress(total, i + 1, module, name));
            }
            catch (InvalidOperationException ex)
            {
                errors.Add($"Failed to deserialize value for {key} at scope {r.Scope}{(r.ScopeId is null ? string.Empty : $"({r.ScopeId})")}: {ex.Message}");
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
                progress?.Report(new SettingsProgress(total, i + 1, module, name));
            }
        }

        return new TryGetAllValuesResult<T>(errors.Count == 0, scopedValues, errors);
    }

    /// <summary>
    /// Returns when a setting was last updated for the specified typed key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="settingContext">Optional scope context for the setting.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>
    /// The <see cref="DateTime"/> when the setting was last updated, or <see langword="null"/> if the setting does not exist.
    /// </returns>
    public async Task<DateTime?> GetLastUpdatedTimeAsync<T>(SettingKey<T> key, SettingContext? settingContext = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var scope = settingContext?.Scope ?? SettingScope.Application;
        var scopeId = settingContext?.ScopeId;

        var setting = await this.FindSettingAsync(key.SettingsModule, key.Name, scope, scopeId, ct).ConfigureAwait(false);
        return setting?.UpdatedAt;
    }

    /// <summary>
    /// Clears the cache of settings.
    /// </summary>
    /// <remarks>
    /// This method clears all cached settings, forcing subsequent load operations to retrieve values from the database.
    /// </remarks>
    internal void ClearCache() => this.cache.Clear();

    /// <summary>
    /// Finds a setting entity in the database.
    /// </summary>
    /// <param name="module">The settings module name.</param>
    /// <param name="name">The setting name.</param>
    /// <param name="scope">The setting scope.</param>
    /// <param name="scopeId">Optional scope identifier.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The setting entity or null if not found.</returns>
    private async Task<ModuleSetting?> FindSettingAsync(
        string module,
        string name,
        SettingScope scope,
        string? scopeId,
        CancellationToken ct)
        => await this.context.Settings
            .FirstOrDefaultAsync(
                ms => ms.SettingsModule == module &&
                      ms.Name == name &&
                      ms.Scope == scope &&
                      ms.ScopeId == scopeId,
                ct)
            .ConfigureAwait(false);

    /// <summary>
    /// Processes a single batch item for save or delete.
    /// </summary>
    private async Task ProcessBatchItemAsync(
        string module,
        string name,
        object? value,
        SettingScope scope,
        string? scopeId,
        CancellationToken ct)
    {
        var cacheKey = SettingsCache.GenerateCacheKey(module, name, scope, scopeId);
        if (value is null)
        {
            var existing = await this.FindSettingAsync(module, name, scope, scopeId, ct).ConfigureAwait(false);
            if (existing != null)
            {
                _ = this.context.Settings.Remove(existing);

                // Remove cache entry to avoid returning stale values
                this.cache.Remove(cacheKey);
            }
        }
        else
        {
            var jsonValue = JsonSerializer.Serialize(value, JsonSerializerOptions);
            var existing = await this.FindSettingAsync(module, name, scope, scopeId, ct).ConfigureAwait(false);
            if (existing == null)
            {
                existing = new ModuleSetting
                {
                    SettingsModule = module,
                    Name = name,
                    Scope = scope,
                    ScopeId = scopeId,
                    JsonValue = jsonValue,
                    CreatedAt = DateTime.UtcNow,
                    UpdatedAt = DateTime.UtcNow,
                };
                _ = this.context.Settings.Add(existing);
            }
            else
            {
                existing.JsonValue = jsonValue;
                existing.UpdatedAt = DateTime.UtcNow;
                _ = this.context.Settings.Update(existing);
            }
        }
    }
}

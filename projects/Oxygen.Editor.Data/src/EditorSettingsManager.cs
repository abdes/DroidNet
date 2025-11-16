// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Internal;
using Oxygen.Editor.Data.Models;
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
/// var windowPosition = await settingsManager.LoadSettingAsync<Point>(new Oxygen.Editor.Data.Settings.SettingKey<Point>("MyModule", "WindowPosition"));
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
    /// Resolves a setting across scopes: Project -> Application -> default.
    /// </summary>
    /// <typeparam name="T">The type of the expected setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="projectId">Optional project scope id.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The resolved setting value, or default(T) if not present in any scope.</returns>
    public async Task<T> ResolveSettingAsync<T>(SettingKey<T> key, string? projectId = null, CancellationToken ct = default)
    {
        if (!string.IsNullOrEmpty(projectId))
        {
            var projectValue = await this.LoadSettingAsync(key, SettingContext.Project(projectId), ct).ConfigureAwait(false);
            if (projectValue is not null)
            {
                return projectValue;
            }
        }

        var appValue = await this.LoadSettingAsync(key, SettingContext.Application(), ct).ConfigureAwait(false);
        if (appValue is not null)
        {
            return appValue;
        }

        return default!;
    }

    /// <summary>
    /// Saves a typed setting value for the provided key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="value">The value to save.</param>
    /// <param name="settingContext">Optional scope context.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task that completes once the value has been persisted.</returns>
    public async Task SaveSettingAsync<T>(SettingKey<T> key, T value, SettingContext? settingContext = null, CancellationToken ct = default)
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

        this.cache.Set(cacheKey, value);
        this.notifier.NotifyChange(key.SettingsModule, key.Name, key, default, value, scope, scopeId);
    }

    /// <inheritdoc/>
    public async Task SaveSettingAsync<T>(SettingDescriptor<T> descriptor, T value, SettingContext? settingContext = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        ValidateSetting(descriptor, value);
        await this.SaveSettingAsync(descriptor.Key, value, settingContext, ct).ConfigureAwait(false);
    }

    /// <summary>
    /// Loads a typed setting value for the provided key and scope.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="settingContext">Optional scope context.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The deserialized value or <see langword="null"/> if not present.</returns>
    public async Task<T?> LoadSettingAsync<T>(SettingKey<T> key, SettingContext? settingContext = null, CancellationToken ct = default)
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
            return default;
        }

        var value = setting.JsonValue is null ? default : JsonSerializer.Deserialize<T>(setting.JsonValue, JsonSerializerOptions);
        this.cache.Set(cacheKey, value);
        return value;
    }

    /// <summary>
    /// Loads a typed setting value for the provided key and returns a supplied default value if the setting does not exist.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="key">Typed setting key.</param>
    /// <param name="defaultValue">The default value to return if not present.</param>
    /// <param name="settingContext">Optional scope context.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The deserialized value or the provided default when not present.</returns>
    public async Task<T> LoadSettingAsync<T>(SettingKey<T> key, T defaultValue, SettingContext? settingContext = null, CancellationToken ct = default)
    {
        var value = await this.LoadSettingAsync(key, settingContext, ct).ConfigureAwait(false);
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

    // Legacy API removed in v-next. Consumers should use WhenSettingChanged<T> to observe changes.

    /// <summary>
    /// Creates a batch transaction for saving multiple settings atomically.
    /// Uses RAII pattern - dispose the batch to commit.
    /// </summary>
    /// <param name="context">The context (scope) for all batch operations. Defaults to Application scope.</param>
    /// <param name="progress">Optional progress reporter.</param>
    /// <returns>
    /// An <see cref="ISettingsBatch"/> instance representing the batch transaction.
    /// Dispose the returned object to commit the batch.
    /// </returns>
    public ISettingsBatch BeginBatch(SettingContext? context = null, IProgress<SettingsSaveProgress>? progress = null)
        => new SettingsBatch(this, context ?? SettingContext.Application(), progress);

    /// <summary>
    /// Save a batch of settings in a single transaction.
    /// Legacy API - prefer using <see cref="BeginBatch"/> with RAII pattern.
    /// </summary>
    /// <param name="configure">Action to configure the batch to save or remove settings.</param>
    /// <param name="progress">Optional progress callback used to report progress.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task that completes once the batch has been committed.</returns>
    /// <exception cref="SettingsValidationException">Thrown when any batch item fails validation.</exception>
    public async Task SaveSettingsAsync(Action<ISettingsBatch> configure, IProgress<SettingsSaveProgress>? progress = null, CancellationToken ct = default)
    {
        var batch = this.BeginBatch(progress: progress);
        await using (batch.ConfigureAwait(false))
        {
            configure(batch);
        }
    }

    /// <inheritdoc/>
    public Task<IReadOnlyDictionary<string, IReadOnlyList<ISettingDescriptor>>> GetDescriptorsByCategoryAsync(CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var dict = this.GetDescriptorsByCategory();
        return Task.FromResult<IReadOnlyDictionary<string, IReadOnlyList<ISettingDescriptor>>>(dict);
    }

    /// <inheritdoc/>
    public Task<IReadOnlyList<ISettingDescriptor>> SearchDescriptorsAsync(string searchTerm, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var matches = this.SearchDescriptors(searchTerm);
        return Task.FromResult<IReadOnlyList<ISettingDescriptor>>(matches);
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<string>> GetAllKeysAsync(CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var keys = await this.context.Settings.Select(s => s.SettingsModule + "/" + s.Name).Distinct().ToListAsync(ct).ConfigureAwait(false);
        return keys;
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<(SettingScope scope, string? scopeId, object? value)>> GetAllValuesAsync(string key, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(key))
        {
            return Array.Empty<(SettingScope, string?, object?)>();
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
        var list = new List<(SettingScope, string?, object?)>(results.Count);
        foreach (var r in results)
        {
            var val = r.JsonValue is null ? null : JsonSerializer.Deserialize<object>(r.JsonValue, JsonSerializerOptions);
            list.Add((r.Scope, r.ScopeId, val));
        }

        return list;
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<(SettingScope scope, string? scopeId, T? value)>> GetAllValuesAsync<T>(string key, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(key))
        {
            return Array.Empty<(SettingScope, string?, T?)>();
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
        var list = new List<(SettingScope, string?, T?)>(results.Count);
        foreach (var r in results)
        {
            var val = r.JsonValue is null ? default : JsonSerializer.Deserialize<T>(r.JsonValue, JsonSerializerOptions);
            list.Add((r.Scope, r.ScopeId, val));
        }

        return list;
    }

    /// <inheritdoc/>
    public async Task<TryGetAllValuesResult<T>> TryGetAllValuesAsync<T>(string key, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var errors = new List<string>();
        var scopedValues = new List<ScopedValue<T>>();

        if (string.IsNullOrWhiteSpace(key))
        {
            errors.Add("Key is null or whitespace");
            return new TryGetAllValuesResult<T>(false, scopedValues, errors);
        }

        var parts = key.Split('/', 2);
        if (parts.Length != 2)
        {
            errors.Add($"Invalid setting key: '{key}'. Expected format: Module/Name");
            return new TryGetAllValuesResult<T>(false, scopedValues, errors);
        }

        var module = parts[0];
        var name = parts[1];
        var query = this.context.Settings.Where(ms => ms.SettingsModule == module && ms.Name == name);
        var results = await query.Select(ms => new { ms.Scope, ms.ScopeId, ms.JsonValue }).ToListAsync(ct).ConfigureAwait(false);

        foreach (var r in results)
        {
            if (r.JsonValue is null)
            {
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
                continue;
            }

            try
            {
                var val = JsonSerializer.Deserialize<T>(r.JsonValue, JsonSerializerOptions);
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, val));
            }
            catch (System.Text.Json.JsonException ex)
            {
                errors.Add($"Failed to deserialize value for {key} at scope {r.Scope}{(r.ScopeId is null ? string.Empty : $"({r.ScopeId})")}: {ex.Message}");
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
            }
            catch (NotSupportedException ex)
            {
                errors.Add($"Failed to deserialize value for {key} at scope {r.Scope}{(r.ScopeId is null ? string.Empty : $"({r.ScopeId})")}: {ex.Message}");
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
            }
            catch (InvalidOperationException ex)
            {
                errors.Add($"Failed to deserialize value for {key} at scope {r.Scope}{(r.ScopeId is null ? string.Empty : $"({r.ScopeId})")}: {ex.Message}");
                scopedValues.Add(new ScopedValue<T>(r.Scope, r.ScopeId, default));
            }
        }

        return new TryGetAllValuesResult<T>(errors.Count == 0, scopedValues, errors);
    }

    /// <summary>
    /// Clears the cache of settings.
    /// </summary>
    /// <remarks>
    /// This method clears all cached settings, forcing subsequent load operations to retrieve values from the database.
    /// </remarks>
    internal void ClearCache() => this.cache.Clear();

    /// <summary>
    /// Commits a batch of settings operations atomically.
    /// </summary>
    /// <param name="items">The batch items to commit.</param>
    /// <param name="progress">Optional progress reporter.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task that completes when the batch is committed.</returns>
    /// <exception cref="SettingsValidationException">Thrown when validation fails.</exception>
    internal async Task CommitBatchAsync(IReadOnlyList<BatchItem> items, IProgress<SettingsSaveProgress>? progress, CancellationToken ct)
    {
        ValidateBatchItems(items);

        var transaction = await this.context.Database.BeginTransactionAsync(ct).ConfigureAwait(false);
        await using (transaction.ConfigureAwait(false))
        {
            try
            {
                var total = items.Count;
                var index = 0;
                foreach (var item in items)
                {
                    index++;
                    await this.ProcessBatchItemAsync(item.Module, item.Name, item.Value, item.Scope, item.ScopeId, ct).ConfigureAwait(false);
                    _ = await this.context.SaveChangesAsync(ct).ConfigureAwait(false);
                    progress?.Report(new SettingsSaveProgress(total, index, item.Module));
                }

                await transaction.CommitAsync(ct).ConfigureAwait(false);
            }
            catch
            {
                await transaction.RollbackAsync(ct).ConfigureAwait(false);
                throw;
            }
        }
    }

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
    {
        return await this.context.Settings
            .FirstOrDefaultAsync(
                ms => ms.SettingsModule == module &&
                      ms.Name == name &&
                      ms.Scope == scope &&
                      ms.ScopeId == scopeId,
                ct)
            .ConfigureAwait(false);
    }

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
        if (value is null)
        {
            var existing = await this.FindSettingAsync(module, name, scope, scopeId, ct).ConfigureAwait(false);
            if (existing != null)
            {
                _ = this.context.Settings.Remove(existing);
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

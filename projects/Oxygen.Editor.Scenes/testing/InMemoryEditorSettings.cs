// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Scenes.Testing;

/// <summary>
/// An <see cref="IEditorSettingsManager"/> that keeps typed settings in memory, one value per key and settings context:
/// what a service saves a test reads back, and what a test stores the service loads.
/// </summary>
/// <remarks>
/// Only loading and saving are supported; the other members throw <see cref="NotSupportedException"/>, so a test that needs
/// more than a service's stored state learns it at once.
/// </remarks>
public sealed class InMemoryEditorSettings : IEditorSettingsManager
{
    private readonly Lock gate = new();
    private readonly Dictionary<(string Module, string Name, SettingContext? Context), object?> values = [];

    /// <summary>
    /// Gets or sets a step every load completes first, which holds a load until the test lets it go; by default, none.
    /// </summary>
    /// <remarks>The load reads the stored value after the step, so a test may store or change it while the load waits.</remarks>
    public Func<Task>? BeforeLoad { get; set; }

    /// <summary>Gets what is stored for a key in a settings context.</summary>
    /// <typeparam name="T">The setting's type.</typeparam>
    /// <param name="key">The setting.</param>
    /// <param name="context">The settings context.</param>
    /// <returns>The stored value, or <see langword="default"/> when nothing is stored.</returns>
    public T? Stored<T>(SettingKey<T> key, SettingContext? context)
    {
        lock (this.gate)
        {
            return this.values.TryGetValue((key.SettingsModule, key.Name, context), out var value) ? (T?)value : default;
        }
    }

    /// <summary>Stores a value, as if an earlier run of the editor had saved it.</summary>
    /// <typeparam name="T">The setting's type.</typeparam>
    /// <param name="key">The setting.</param>
    /// <param name="context">The settings context.</param>
    /// <param name="value">The value.</param>
    public void Store<T>(SettingKey<T> key, SettingContext? context, T value)
    {
        lock (this.gate)
        {
            this.values[(key.SettingsModule, key.Name, context)] = value;
        }
    }

    /// <summary>Gets the settings contexts a key has a stored value in.</summary>
    /// <typeparam name="T">The setting's type.</typeparam>
    /// <param name="key">The setting.</param>
    /// <returns>The contexts, in no particular order.</returns>
    public IReadOnlyCollection<SettingContext?> ContextsOf<T>(SettingKey<T> key)
    {
        lock (this.gate)
        {
            return
            [
                .. this.values.Keys
                    .Where(stored => string.Equals(stored.Module, key.SettingsModule, StringComparison.Ordinal) && string.Equals(stored.Name, key.Name, StringComparison.Ordinal))
                    .Select(static stored => stored.Context),
            ];
        }
    }

    /// <inheritdoc/>
    public Task SaveSettingAsync<T>(SettingKey<T> key, T value, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        this.Store(key, settingContext, value);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public async Task<T?> LoadSettingAsync<T>(SettingKey<T> key, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
    {
        if (this.BeforeLoad is { } before)
        {
            await before().ConfigureAwait(false);
        }

        ct.ThrowIfCancellationRequested();
        return this.Stored(key, settingContext);
    }

    /// <inheritdoc/>
    public async Task<T> LoadSettingOrDefaultAsync<T>(SettingKey<T> key, T defaultValue, SettingContext? settingContext = null, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
        => await this.LoadSettingAsync(key, settingContext, progress, ct).ConfigureAwait(false) is { } value ? value : defaultValue;

    /// <inheritdoc/>
    public IObservable<SettingChangedEvent<T>> WhenSettingChanged<T>(SettingKey<T> key) => throw Unsupported();

    /// <inheritdoc/>
    public Task<IReadOnlyList<SettingScope>> GetDefinedScopesAsync<T>(SettingKey<T> key, string? projectId = null, CancellationToken ct = default)
        => throw Unsupported();

    /// <inheritdoc/>
    public IReadOnlyDictionary<string, IReadOnlyList<ISettingDescriptor>> GetDescriptorsByCategory() => throw Unsupported();

    /// <inheritdoc/>
    public IReadOnlyList<ISettingDescriptor> SearchDescriptors(string searchTerm) => throw Unsupported();

    /// <inheritdoc/>
    public Task<IReadOnlyList<string>> GetAllKeysAsync(CancellationToken ct = default) => throw Unsupported();

    /// <inheritdoc/>
    public Task<IReadOnlyList<(SettingScope Scope, string? ScopeId, object? Value)>> GetAllValuesAsync(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
        => throw Unsupported();

    /// <inheritdoc/>
    public Task<IReadOnlyList<(SettingScope Scope, string? ScopeId, T? Value)>> GetAllValuesAsync<T>(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
        => throw Unsupported();

    /// <inheritdoc/>
    public Task<TryGetAllValuesResult<T>> TryGetAllValuesAsync<T>(string key, IProgress<SettingsProgress>? progress = null, CancellationToken ct = default)
        => throw Unsupported();

    /// <inheritdoc/>
    public Task<DateTime?> GetLastUpdatedTimeAsync<T>(SettingKey<T> key, SettingContext? settingContext = null, CancellationToken ct = default)
        => throw Unsupported();

    private static NotSupportedException Unsupported()
        => new("InMemoryEditorSettings keeps and loads values only.");
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Config;

/// <summary>
///     An implementation of the <see cref="ISettingsManager"/> interface, that implements last-loaded-wins strategy for
///     multi-source composition.
/// </summary>
/// <remarks>
///     This implementation follows the contract specified in <see cref="ISettingsManager"/>, and handles multi-source
///     composition using a last-loaded-wins strategy. When initialized, its loads all registered sources in the order
///     they were added. Settings sections from later-loaded sources override those from earlier ones when there are
///     conflicts.
///     <para>
///     This implementation supports reloading, adding and removing sources at runtime via <see cref="AddSourceAsync"/>
///     and <see cref="RemoveSourceAsync"/> and automatic or manually triggered reloads via <see cref="ReloadAllAsync"/>.
///     Updates resulting from these operations are propagated to all active settings services, but it remains the
///     responsibility of the service implementation to honor them.
///     </para>
///     <para>
///     Applications can subscribe to the <see cref="SourceChanged"/> event to be notified of any lifecycle changes
///     occurring to the managed sources.
///     </para>
/// </remarks>
/// <note>
///     When a source is configured, it stays configured until it is explicitly removed, even if it fails to load. This
///     allows it to be retried on subsequent reloads or when its data changes and the source is being watched.
/// </note>
/// <param name="sources">The collection of settings sources to manage.</param>
/// <param name="resolver">The DI container resolver for creating service instances.</param>
/// <param name="loggerFactory">Optional logger factory used to create a logger for diagnostic output.</param>
public sealed partial class SettingsManager(
    IEnumerable<ISettingsSource> sources,
    IResolver resolver,
    ILoggerFactory? loggerFactory = null) : ISettingsManager
{
    private readonly ILogger<SettingsManager> logger = loggerFactory?.CreateLogger<SettingsManager>() ?? NullLogger<SettingsManager>.Instance;
    private readonly List<ISettingsSource> sources = sources?.ToList() ?? throw new ArgumentNullException(nameof(sources));
    private readonly IResolver resolver = resolver ?? throw new ArgumentNullException(nameof(resolver));
    private readonly ConcurrentDictionary<Type, object> serviceInstances = new();
    private readonly Dictionary<string, object> cachedSections = new(StringComparer.Ordinal);
    private readonly AsyncLock initializationLock = new();
    private bool isInitialized;
    private bool isDisposed;

    /// <inheritdoc/>
    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    /// <inheritdoc/>
    public IReadOnlyList<ISettingsSource> Sources
    {
        get
        {
            this.ThrowIfDisposed();
            return this.sources.AsReadOnly();
        }
    }

    /// <inheritdoc/>
    public async Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            if (this.isInitialized)
            {
                this.LogAlreadyInitialized();
                return;
            }

            this.LogInitializing(this.sources.Count);

            foreach (var source in this.sources)
            {
                await this.TryLoadSourceAsync(source, false, cancellationToken).ConfigureAwait(false);
            }

            this.isInitialized = true;
            this.LogInitializationComplete();
        }
    }

    /// <inheritdoc/>
    public ISettingsService<TSettingsInterface> GetService<TSettingsInterface>()
        where TSettingsInterface : class
    {
        this.ThrowIfDisposed();
        this.ThrowIfNotInitialized();

        var settingsType = typeof(TSettingsInterface);

        // Return existing instance if available
        if (this.serviceInstances.TryGetValue(settingsType, out var existingService))
        {
            return (ISettingsService<TSettingsInterface>)existingService;
        }

        this.LogCreatingService(settingsType.Name);

        // Get the concrete service instance from the resolver, keyed by the settings type
        var service = this.resolver.Resolve<ISettingsService<TSettingsInterface>>(serviceKey: "__uninitialized__");
        this.ApplySettings(service);
        var added = this.serviceInstances.TryAdd(settingsType, service);
        Debug.Assert(added, "Service instance should have been added successfully.");

        return service;
    }

    private void ApplySettings<TSettingsInterface>(ISettingsService<TSettingsInterface> service)
        where TSettingsInterface : class
    {
        var sectionName = service.SectionName;
        var settings = service.Settings;

        // Use cached sections data instead of re-reading from sources
        if (this.cachedSections.TryGetValue(sectionName, out var sectionData))
        {
            if (sectionData is System.Text.Json.JsonElement jsonElement)
            {
                // Deserialize JsonElement to the POCO type first
                // Note: Do NOT use CamelCase policy - the JSON is already in the correct case
                var deserializedData = System.Text.Json.JsonSerializer.Deserialize(
                    jsonElement.GetRawText(),
                    service.SettingsType);

                // Cast the POCO instance to TSettings (works if POCO implements TSettings interface)
                if (deserializedData is TSettingsInterface typedInstance)
                {
                    ApplyProperties(typedInstance, settings);
                    this.LogLoadedSettingsForType(sectionName, "cache (deserialized from JsonElement)");
                }
                else
                {
                    this.LogSettingsDeserializationFailed(sectionName, service.SettingsType);
                }

            }
        }

    }

    /// <inheritdoc/>
    public async Task ReloadAllAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        this.ThrowIfNotInitialized();

        this.LogReloadingAllSources();

        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            this.cachedSections.Clear();

            foreach (var source in this.sources)
            {
                await this.TryLoadSourceAsync(source, true, cancellationToken).ConfigureAwait(false);
            }

            this.NotifyServicesOfReload();
        }
    }

    /// <inheritdoc/>
    public async Task AddSourceAsync(ISettingsSource source, CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(source);

        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            if (this.sources.Any(s => string.Equals(s.Id, source.Id, StringComparison.Ordinal)))
            {
                throw new InvalidOperationException($"Source with ID '{source.Id}' already exists.");
            }

            this.sources.Add(source);
            this.LogAddedSource(source.Id);

            // Load the new source if manager is already initialized
            if (this.isInitialized)
            {
                await this.TryLoadSourceAsync(source, false, cancellationToken).ConfigureAwait(false);
            }
        }
    }

    /// <inheritdoc/>
    public async Task RemoveSourceAsync(string sourceId, CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(sourceId);

        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            // Find and remove in one pass
            var index = this.sources.FindIndex(s => string.Equals(s.Id, sourceId, StringComparison.Ordinal));
            if (index < 0)
            {
                throw new InvalidOperationException($"Source with ID '{sourceId}' not found.");
            }

            var source = this.sources[index];
            this.sources.RemoveAt(index);

            this.LogRemovedSource(sourceId);
            this.OnSourceChanged(new SourceChangedEventArgs(sourceId, SourceChangeType.Removed));

            if (source is IDisposable disposable)
            {
                disposable.Dispose();
            }
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.LogDisposingManager();

        // Dispose all service instances
        foreach (var serviceInstance in this.serviceInstances.Values)
        {
            if (serviceInstance is IDisposable disposable)
            {
                disposable.Dispose();
            }
        }

        this.serviceInstances.Clear();

        // Dispose all sources
        foreach (var source in this.sources)
        {
            if (source is IDisposable disposable)
            {
                disposable.Dispose();
            }
        }

        this.sources.Clear();

        this.initializationLock.Dispose();
        this.isDisposed = true;
    }

    /// <summary>
    /// Loads settings data from all sources for a specific settings type.
    /// Applies last-loaded-wins strategy.
    /// </summary>
    /// <typeparam name="TSettings">The settings type.</typeparam>
    /// <param name="sectionName">The section name to load from the cached data.</param>
    /// <param name="pocoType">The concrete POCO type to deserialize to when data is stored as JsonElement. If null, defaults to typeof(TSettings).</param>
    /// <param name="cancellationToken">Cancellation token.</param>
    /// <returns>Merged settings data, or null if no data found for the section.</returns>
    internal async Task<TSettings?> LoadSettingsAsync<TSettings>(string sectionName, Type? pocoType = null, CancellationToken cancellationToken = default)
        where TSettings : class
    {
        await Task.CompletedTask.ConfigureAwait(false); // Keep method async for consistency
        cancellationToken.ThrowIfCancellationRequested();

        TSettings? mergedSettings = null;
        pocoType ??= typeof(TSettings);

        // Use cached sections data instead of re-reading from sources
        if (this.cachedSections.TryGetValue(sectionName, out var sectionData))
        {
            if (sectionData is TSettings typedData)
            {
                mergedSettings = typedData;
                this.LogLoadedSettingsForType(sectionName, "cache");
            }
            else if (sectionData is System.Text.Json.JsonElement jsonElement)
            {
                // Deserialize JsonElement to the POCO type first
                // Note: Do NOT use CamelCase policy - the JSON is already in the correct case
                var pocoInstance = System.Text.Json.JsonSerializer.Deserialize(
                    jsonElement.GetRawText(),
                    pocoType);

                // Cast the POCO instance to TSettings (works if POCO implements TSettings interface)
                if (pocoInstance is TSettings typedInstance)
                {
                    mergedSettings = typedInstance;
                }

                this.LogLoadedSettingsForType(sectionName, "cache (deserialized from JsonElement)");
            }
        }

        // Return null if no settings were found - the service will handle creating defaults
        return mergedSettings;
    }

    /// <summary>
    /// Saves settings data to all writable sources.
    /// </summary>
    /// <typeparam name="TSettings">The settings type.</typeparam>
    /// <param name="sectionName">The section name to use when saving.</param>
    /// <param name="settings">The settings data to save.</param>
    /// <param name="metadata">Metadata for the settings.</param>
    /// <param name="cancellationToken">Cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    internal async Task SaveSettingsAsync<TSettings>(
        string sectionName,
        TSettings settings,
        SettingsMetadata metadata,
        CancellationToken cancellationToken = default)
        where TSettings : class
    {
        var sectionsData = new Dictionary<string, object>(StringComparer.Ordinal) { [sectionName] = settings };

        foreach (var source in this.sources)
        {
            try
            {
                var result = await source.SaveAsync(sectionsData, metadata, cancellationToken).ConfigureAwait(false);
                if (result.IsSuccess)
                {
                    this.cachedSections[sectionName] = settings!;
                    this.LogSavedSettingsForType(sectionName, source.Id);
                    this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Updated));
                }
                else
                {
                    this.LogFailedToSaveSettingsForType(sectionName, source.Id, result.Error.Message);
                }
            }
            catch (Exception ex)
            {
                this.LogExceptionSavingSettingsForType(ex, sectionName, source.Id);
                throw new SettingsPersistenceException(
                    $"Failed to save settings to source '{source.Id}'",
                    source.Id,
                    ex);
            }
        }
    }

    private static void ApplyProperties<T>(T source, T target)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(target);

        var props = typeof(T)
            .GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .Where(p => p.CanRead && p.CanWrite && p.GetIndexParameters().Length == 0);

        foreach (var prop in props)
        {
            var value = prop.GetValue(source);
            prop.SetValue(target, value); // goes through the setter → triggers notifications
        }
    }

    private void OnSourceChanged(SourceChangedEventArgs args)
    {
        this.SourceChanged?.Invoke(this, args);
    }

    private async Task TryLoadSourceAsync(ISettingsSource source, bool allowReload, CancellationToken cancellationToken)
    {
        this.LogLoadingSource(source.Id);
        var loadResult = await source.LoadAsync(reload: allowReload, cancellationToken).ConfigureAwait(false);
        _ = loadResult.Tap(
            payload =>
            {
                // On success: cache sections, and raise change event
                foreach (var (sectionName, sectionData) in payload.Sections)
                {
                    // On success, neither the section name nor data should be null
                    Debug.Assert(sectionName is { Length: > 0 }, "Section name should not be null or empty.");
                    Debug.Assert(sectionData is { }, "Section data should not be null.");
                    this.cachedSections[sectionName] = sectionData;
                }

                this.LogLoadedSource(source.Id);
                this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Added));
            },
            err => this.LogFailedToLoadSource(source.Id, err.Message));
    }

    private void NotifyServicesOfReload()
    {
        foreach (var serviceInstance in this.serviceInstances.Values)
        {
            if (serviceInstance is IAsyncDisposable)
            {
                // Services will handle their own reload internally
                this.LogServiceHandlesReload(serviceInstance.GetType().Name);
            }
        }
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, nameof(SettingsManager));

    private void ThrowIfNotInitialized()
    {
        if (!this.isInitialized)
        {
            throw new InvalidOperationException("SettingsManager must be initialized.");
        }
    }

    public sealed class AsyncLock : IDisposable, IAsyncDisposable
    {
        private readonly SemaphoreSlim semaphore = new(1, 1);
        private bool disposed;

        public async ValueTask<Releaser> AcquireAsync(CancellationToken ct = default)
        {
            this.ThrowIfDisposed();
            await this.semaphore.WaitAsync(ct).ConfigureAwait(false);
            return new Releaser(this.semaphore);
        }

        public void Dispose()
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.semaphore.Dispose();
        }

        public ValueTask DisposeAsync()
        {
            this.Dispose(); // SemaphoreSlim has only sync Dispose
            return ValueTask.CompletedTask;
        }

        private void ThrowIfDisposed()
            => ObjectDisposedException.ThrowIf(this.disposed, nameof(AsyncLock));

        public readonly struct Releaser(SemaphoreSlim toRelease) : IDisposable, IAsyncDisposable
        {
            public void Dispose() => toRelease?.Release();

            public ValueTask DisposeAsync()
            {
                toRelease?.Release();
                return ValueTask.CompletedTask;
            }
        }
    }
}

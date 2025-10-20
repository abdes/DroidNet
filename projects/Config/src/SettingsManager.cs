// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Config;

/// <summary>
/// Central orchestrator that manages all settings sources and provides access to settings services.
/// Implements last-loaded-wins strategy for multi-source composition.
/// </summary>
/// <remarks>
/// Initializes a new instance of the <see cref="SettingsManager"/> class.
/// </remarks>
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
    private readonly SemaphoreSlim initializationLock = new(1, 1);
    private readonly List<IDisposable> subscriptions = [];
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

        await this.initializationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (this.isInitialized)
            {
                LogAlreadyInitialized(this.logger);
                return;
            }

            LogInitializing(this.logger, this.sources.Count);

            // Load all sources in order (last-loaded-wins)
            foreach (var source in this.sources)
            {
                try
                {
                    LogLoadingSource(this.logger, source.Id);
                    var result = await source.LoadAsync(false, cancellationToken).ConfigureAwait(false);

                    if (result.IsSuccess)
                    {
                        var payload = result.Value;

                        foreach (var (sectionName, sectionData) in payload.Sections)
                        {
                            if (sectionData != null)
                            {
                                this.cachedSections[sectionName] = sectionData;
                            }
                        }

                        LogLoadedSource(this.logger, source.Id);
                        this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Added));
                    }
                    else
                    {
                        var errorMessage = result.Error.Message;
                        LogFailedToLoadSource(this.logger, source.Id, errorMessage);
                    }
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch (Exception ex)
                {
                    LogExceptionLoadingSource(this.logger, ex, source.Id);
                }
#pragma warning restore CA1031 // Do not catch general exception types
            }

            this.isInitialized = true;
            LogInitializationComplete(this.logger);
        }
        finally
        {
            _ = this.initializationLock.Release();
        }
    }

    /// <inheritdoc/>
    public ISettingsService<TSettings> GetService<TSettings>()
        where TSettings : class
    {
        this.ThrowIfDisposed();

        if (!this.isInitialized)
        {
            throw new InvalidOperationException(
                "SettingsManager must be initialized before getting services. Call InitializeAsync first.");
        }

        var settingsType = typeof(TSettings);

        // Return existing instance if available
        if (this.serviceInstances.TryGetValue(settingsType, out var existingService))
        {
            return (ISettingsService<TSettings>)existingService;
        }

        // Create new service instance using DI container
        LogCreatingService(this.logger, settingsType.Name);

        var service = this.resolver.Resolve<ISettingsService<TSettings>>();
        _ = this.serviceInstances.TryAdd(settingsType, service);

        return service;
    }

    /// <inheritdoc/>
    public async Task ReloadAllAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        if (!this.isInitialized)
        {
            throw new InvalidOperationException("SettingsManager must be initialized before reloading.");
        }

        LogReloadingAllSources(this.logger);

        await this.initializationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await this.ReloadSourcesAsync(cancellationToken).ConfigureAwait(false);
            this.NotifyServicesOfReload();
        }
        finally
        {
            _ = this.initializationLock.Release();
        }
    }

    /// <inheritdoc/>
    public async Task RunMigrationsAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        if (!this.isInitialized)
        {
            throw new InvalidOperationException("SettingsManager must be initialized before running migrations.");
        }

        LogRunningMigrations(this.logger);

        foreach (var serviceInstance in this.serviceInstances.Values)
        {
            if (serviceInstance is ISettingsService<object> service)
            {
                try
                {
                    await service.RunMigrationsAsync(cancellationToken).ConfigureAwait(false);
                }
                catch (Exception ex)
                {
                    LogMigrationFailed(this.logger, ex, serviceInstance.GetType().Name);
                    throw;
                }
            }
        }
    }

    /// <inheritdoc/>
    public async Task AddSourceAsync(ISettingsSource source, CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(source);

        await this.initializationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (this.sources.Any(s => string.Equals(s.Id, source.Id, StringComparison.Ordinal)))
            {
                throw new InvalidOperationException($"Source with ID '{source.Id}' already exists.");
            }

            this.sources.Add(source);
            LogAddedSource(this.logger, source.Id);

            // Load the new source if manager is already initialized
            if (this.isInitialized)
            {
                var result = await source.LoadAsync(false, cancellationToken).ConfigureAwait(false);
                if (result.IsSuccess)
                {
                    foreach (var (sectionName, sectionData) in result.Value.Sections)
                    {
                        if (sectionData != null)
                        {
                            this.cachedSections[sectionName] = sectionData;
                        }
                    }

                    this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Added));
                }
                else
                {
                    LogFailedToLoadSource(this.logger, source.Id, result.Error.Message);
                }
            }
        }
        finally
        {
            _ = this.initializationLock.Release();
        }
    }

    /// <inheritdoc/>
    public async Task RemoveSourceAsync(string sourceId, CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(sourceId);

        await this.initializationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var source = this.sources.FirstOrDefault(s => string.Equals(s.Id, sourceId, StringComparison.Ordinal)) ?? throw new InvalidOperationException($"Source with ID '{sourceId}' not found.");
            _ = this.sources.Remove(source);
            LogRemovedSource(this.logger, sourceId);

            this.OnSourceChanged(new SourceChangedEventArgs(sourceId, SourceChangeType.Removed));

            // Dispose the source if it's disposable
            if (source is IDisposable disposable)
            {
                disposable.Dispose();
            }
        }
        finally
        {
            _ = this.initializationLock.Release();
        }
    }

    /// <inheritdoc/>
    public IDisposable SubscribeToSourceChanges(Action<SourceChangedEventArgs> handler)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(handler);

        this.SourceChanged += EventHandler;

        var subscription = new Subscription(() => this.SourceChanged -= EventHandler);
        this.subscriptions.Add(subscription);

        return subscription;

        void EventHandler(object? sender, SourceChangedEventArgs args) => handler(args);
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        LogDisposingManager(this.logger);

        // Dispose all service instances
        foreach (var serviceInstance in this.serviceInstances.Values)
        {
            if (serviceInstance is IDisposable disposable)
            {
                disposable.Dispose();
            }
        }

        this.serviceInstances.Clear();

        // Dispose all subscriptions
        foreach (var subscription in this.subscriptions)
        {
            subscription.Dispose();
        }

        this.subscriptions.Clear();

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
                LogLoadedSettingsForType(this.logger, sectionName, "cache");
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

                LogLoadedSettingsForType(this.logger, sectionName, "cache (deserialized from JsonElement)");
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
                    LogSavedSettingsForType(this.logger, sectionName, source.Id);
                    this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Updated));
                }
                else
                {
                    LogFailedToSaveSettingsForType(this.logger, sectionName, source.Id, result.Error.Message);
                }
            }
            catch (Exception ex)
            {
                LogExceptionSavingSettingsForType(this.logger, ex, sectionName, source.Id);
                throw new SettingsPersistenceException(
                    $"Failed to save settings to source '{source.Id}'",
                    source.Id,
                    ex);
            }
        }
    }

    private void OnSourceChanged(SourceChangedEventArgs args)
    {
        this.SourceChanged?.Invoke(this, args);
    }

    private async Task ReloadSourcesAsync(CancellationToken cancellationToken)
    {
        this.cachedSections.Clear();

        foreach (var source in this.sources)
        {
            try
            {
                var result = await source.LoadAsync(true, cancellationToken).ConfigureAwait(false);

                if (result.IsSuccess)
                {
                    var payload = result.Value;

                    foreach (var (sectionName, sectionData) in payload.Sections)
                    {
                        if (sectionData != null)
                        {
                            this.cachedSections[sectionName] = sectionData;
                        }
                    }

                    LogReloadedSource(this.logger, source.Id);
                    this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Updated));
                }
                else
                {
                    LogFailedToReloadSource(this.logger, source.Id, result.Error.Message);
                }
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                LogExceptionReloadingSource(this.logger, ex, source.Id);
            }
#pragma warning restore CA1031 // Do not catch general exception types
        }
    }

    private void NotifyServicesOfReload()
    {
        foreach (var serviceInstance in this.serviceInstances.Values)
        {
            if (serviceInstance is IAsyncDisposable)
            {
                // Services will handle their own reload internally
                LogServiceHandlesReload(this.logger, serviceInstance.GetType().Name);
            }
        }
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, nameof(SettingsManager));

    private sealed class Subscription(Action unsubscribe) : IDisposable
    {
        private readonly Action unsubscribe = unsubscribe;
        private bool isDisposed;

        public void Dispose()
        {
            if (!this.isDisposed)
            {
                this.unsubscribe();
                this.isDisposed = true;
            }
        }
    }
}

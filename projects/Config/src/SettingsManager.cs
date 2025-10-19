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
    public event EventHandler<SettingsSourceChangedEventArgs>? SourceChanged;

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

        Console.WriteLine($"[DEBUG] SettingsManager.InitializeAsync called, sources count: {this.sources.Count}, already initialized: {this.isInitialized}");

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
                    Console.WriteLine($"[DEBUG InitializeAsync] About to read source '{source.Id}'");
                    LogLoadingSource(this.logger, source.Id);
                    var result = await source.ReadAsync(cancellationToken).ConfigureAwait(false);
                    Console.WriteLine($"[DEBUG InitializeAsync] Source '{source.Id}' read result: Success={result.Success}, SectionsData={result.SectionsData?.Count ?? 0} sections");

                    if (result.Success)
                    {
                        // Cache the sections data from this source (last-loaded-wins)
                        if (result.SectionsData != null)
                        {
                            Console.WriteLine($"[DEBUG InitializeAsync] Source '{source.Id}' returned {result.SectionsData.Count} sections:");
                            foreach (var (sectionName, sectionData) in result.SectionsData)
                            {
                                Console.WriteLine($"[DEBUG InitializeAsync]   Caching section '{sectionName}', data type: {sectionData?.GetType().Name ?? "null"}");
                                if (sectionData != null)
                                {
                                    this.cachedSections[sectionName] = sectionData;
                                }
                            }
                        }

                        LogLoadedSource(this.logger, source.Id);
                        this.OnSourceChanged(new SettingsSourceChangedEventArgs(source.Id, SettingsSourceChangeType.Added));
                    }
                    else
                    {
                        Console.WriteLine($"[DEBUG InitializeAsync] Source '{source.Id}' FAILED: {result.ErrorMessage}");
                        LogFailedToLoadSource(this.logger, source.Id, result.ErrorMessage);
                        this.OnSourceChanged(
                            new SettingsSourceChangedEventArgs(
                                source.Id,
                                SettingsSourceChangeType.Failed,
                                result.ErrorMessage));
                    }
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch (Exception ex)
                {
                    Console.WriteLine($"[DEBUG InitializeAsync] EXCEPTION reading source '{source.Id}': {ex.Message}");
                    LogExceptionLoadingSource(this.logger, ex, source.Id);
                    this.OnSourceChanged(
                        new SettingsSourceChangedEventArgs(source.Id, SettingsSourceChangeType.Failed, ex.Message));
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
            // Clear the cache before reloading
            this.cachedSections.Clear();

            foreach (var source in this.sources)
            {
                try
                {
                    var result = await source.ReadAsync(cancellationToken).ConfigureAwait(false);

                    if (result.Success)
                    {
                        // Update cache with reloaded data (last-loaded-wins)
                        if (result.SectionsData != null)
                        {
                            foreach (var (sectionName, sectionData) in result.SectionsData)
                            {
                                if (sectionData != null)
                                {
                                    this.cachedSections[sectionName] = sectionData;
                                }
                            }
                        }

                        LogReloadedSource(this.logger, source.Id);
                        this.OnSourceChanged(
                            new SettingsSourceChangedEventArgs(source.Id, SettingsSourceChangeType.Updated));
                    }
                    else
                    {
                        LogFailedToReloadSource(this.logger, source.Id, result.ErrorMessage);
                    }
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch (Exception ex)
                {
                    LogExceptionReloadingSource(this.logger, ex, source.Id);
                }
#pragma warning restore CA1031 // Do not catch general exception types
            }

            // Notify all service instances to reload
            foreach (var serviceInstance in this.serviceInstances.Values)
            {
                if (serviceInstance is IAsyncDisposable asyncService)
                {
                    // Services will handle their own reload internally
                    LogServiceHandlesReload(this.logger, serviceInstance.GetType().Name);
                }
            }
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
                var result = await source.ReadAsync(cancellationToken).ConfigureAwait(false);
                if (result.Success)
                {
                    this.OnSourceChanged(new SettingsSourceChangedEventArgs(source.Id, SettingsSourceChangeType.Added));
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

            this.OnSourceChanged(new SettingsSourceChangedEventArgs(sourceId, SettingsSourceChangeType.Removed));

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
    public IDisposable SubscribeToSourceChanges(Action<SettingsSourceChangedEventArgs> handler)
    {
        this.ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(handler);

        this.SourceChanged += EventHandler;

        var subscription = new Subscription(() => this.SourceChanged -= EventHandler);
        this.subscriptions.Add(subscription);

        return subscription;

        void EventHandler(object? sender, SettingsSourceChangedEventArgs args) => handler(args);
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
        Console.WriteLine($"[DEBUG] LoadSettingsAsync: Looking for section '{sectionName}', TSettings={typeof(TSettings).Name}, PocoType={pocoType.Name}");
        Console.WriteLine($"[DEBUG] Cache has {this.cachedSections.Count} sections: {string.Join(", ", this.cachedSections.Keys)}");

        if (this.cachedSections.TryGetValue(sectionName, out var sectionData))
        {
            Console.WriteLine($"[DEBUG] Found section data, type={sectionData?.GetType().Name ?? "null"}");

            if (sectionData is TSettings typedData)
            {
                mergedSettings = typedData;
                Console.WriteLine($"[DEBUG] Direct cast to {typeof(TSettings).Name} succeeded");
                LogLoadedSettingsForType(this.logger, sectionName, "cache");
            }
            else if (sectionData is System.Text.Json.JsonElement jsonElement)
            {
                Console.WriteLine($"[DEBUG] sectionData is JsonElement, attempting to deserialize to {pocoType.Name}");
                Console.WriteLine($"[DEBUG] JsonElement raw text: {jsonElement.GetRawText()}");

                // Deserialize JsonElement to the POCO type first
                // Note: Do NOT use CamelCase policy - the JSON is already in the correct case
                var pocoInstance = System.Text.Json.JsonSerializer.Deserialize(
                    jsonElement.GetRawText(),
                    pocoType);

                Console.WriteLine($"[DEBUG] Deserialization result: {(pocoInstance == null ? "null" : "success")}");

                // Debug: Print properties using reflection
                if (pocoInstance != null)
                {
                    var nameProperty = pocoInstance.GetType().GetProperty("Name");
                    var valueProperty = pocoInstance.GetType().GetProperty("Value");
                    if (nameProperty != null)
                    {
                        Console.WriteLine($"[DEBUG] Deserialized {pocoType.Name}.Name: '{nameProperty.GetValue(pocoInstance)}'");
                    }

                    if (valueProperty != null)
                    {
                        Console.WriteLine($"[DEBUG] Deserialized {pocoType.Name}.Value: {valueProperty.GetValue(pocoInstance)}");
                    }
                }

                // Cast the POCO instance to TSettings (works if POCO implements TSettings interface)
                if (pocoInstance is TSettings typedInstance)
                {
                    mergedSettings = typedInstance;
                    Console.WriteLine($"[DEBUG] Successfully cast {pocoType.Name} to {typeof(TSettings).Name}");
                }
                else
                {
                    Console.WriteLine($"[DEBUG] Failed to cast {pocoType.Name} to {typeof(TSettings).Name}");
                }

                LogLoadedSettingsForType(this.logger, sectionName, "cache (deserialized from JsonElement)");
            }
            else
            {
                Console.WriteLine($"[DEBUG] sectionData is neither {typeof(TSettings).Name} nor JsonElement!");
            }
        }
        else
        {
            Console.WriteLine($"[DEBUG] Section '{sectionName}' NOT found in cache!");
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

        foreach (var source in this.sources.Where(s => s.CanWrite))
        {
            try
            {
                var result = await source.WriteAsync(sectionsData, metadata, cancellationToken).ConfigureAwait(false);
                if (result.Success)
                {
                    LogSavedSettingsForType(this.logger, sectionName, source.Id);
                    this.OnSourceChanged(new SettingsSourceChangedEventArgs(source.Id, SettingsSourceChangeType.Updated));
                }
                else
                {
                    LogFailedToSaveSettingsForType(this.logger, sectionName, source.Id, result.ErrorMessage);
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

    private void OnSourceChanged(SettingsSourceChangedEventArgs args)
    {
        this.SourceChanged?.Invoke(this, args);
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

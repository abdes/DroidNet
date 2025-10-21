// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.Json;
using DroidNet.Config.Sources;
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
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };
    private readonly List<ISettingsSource> sources = sources?.ToList() ?? throw new ArgumentNullException(nameof(sources));
    private readonly IResolver resolver = resolver ?? throw new ArgumentNullException(nameof(resolver));
    private readonly ConcurrentDictionary<Type, ISettingsService> serviceInstances = new();
    private readonly ConcurrentDictionary<string, CachedSection> cachedSections = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, byte> suppressChangeNotificationFor = new(StringComparer.Ordinal);
    private readonly SettingsSourceMetadata.SetterKey metadataSetterKey = new();
    private readonly AsyncLock initializationLock = new();
    private bool isInitialized;
    private bool isDisposed;

    /// <inheritdoc/>
    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    private ServiceUpdateCoordinator ServiceUpdates => new(
        this,
        this.serviceInstances,
        this.cachedSections);

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
                this.SubscribeToSourceEvents(source);
            }

            await this.LoadAllSourcesAsync(allowReload: false, cancellationToken).ConfigureAwait(false);

            this.isInitialized = true;
            this.LogInitializationComplete();
        }
    }

    /// <inheritdoc/>
    public ISettingsService<TSettingsInterface> GetService<TSettingsInterface>()
        where TSettingsInterface : class
    {
        this.EnsureReadyForOperations();

        var settingsType = typeof(TSettingsInterface);
        this.LogGetServiceRequested(settingsType.Name);

        var isNew = false;

        // Use GetOrAdd to atomically get existing or create new service
        var service = (ISettingsService<TSettingsInterface>)this.serviceInstances.GetOrAdd(
            settingsType,
            _ =>
            {
                isNew = true;
                this.LogCreatingService(settingsType.Name);

                // Get the concrete service instance from the resolver, keyed by the settings type
                var newService = this.resolver.Resolve<ISettingsService<TSettingsInterface>>(serviceKey: "__uninitialized__");
                this.ApplySettings(newService);

                return newService;
            });

        if (isNew)
        {
            this.LogServiceRegistered(settingsType.Name, this.serviceInstances.Count);
        }
        else
        {
            this.LogReturningExistingService(settingsType.Name);
        }

        return service;
    }

    /// <inheritdoc/>
    public async Task ReloadAllAsync(CancellationToken cancellationToken = default)
    {
        this.EnsureReadyForOperations();

        this.LogReloadingAllSources();

        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            this.cachedSections.Clear();

            await this.LoadAllSourcesAsync(allowReload: false, cancellationToken).ConfigureAwait(false);

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
            if (this.FindSourceIndexById(source.Id) >= 0)
            {
                throw new InvalidOperationException($"Source with ID '{source.Id}' already exists.");
            }

            this.sources.Add(source);
            this.SubscribeToSourceEvents(source);
            this.LogAddedSource(source.Id);

            // Load the new source if manager is already initialized
            if (this.isInitialized)
            {
                await this.TryLoadSourceAsync(source, allowReload: false, cancellationToken).ConfigureAwait(false);

                // Update any existing services that might be impacted by the new source
                var affectedSections = this.GetSectionsOwnedBySource(source.Id);

                if (affectedSections.Count > 0)
                {
                    var serviceUpdates = this.ServiceUpdates;
                    var impactedServices = serviceUpdates.GetImpactedServices(affectedSections);
                    serviceUpdates.UpdateServices(impactedServices);
                }
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
            var index = this.FindSourceIndexById(sourceId);
            if (index < 0)
            {
                throw new InvalidOperationException($"Source with ID '{sourceId}' not found.");
            }

            var source = this.sources[index];
            this.sources.RemoveAt(index);

            this.UnsubscribeFromSourceEvents(source);
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
            this.UnsubscribeFromSourceEvents(source);

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
        if (this.cachedSections.ContainsKey(sectionName))
        {
            var materialized = this.GetMaterializedSectionData(sectionName, pocoType);
            if (materialized is TSettings typedInstance)
            {
                mergedSettings = typedInstance;
                this.LogLoadedSettingsForType(sectionName, "cache");
            }
        }

        // Return null if no settings were found - the service will handle creating defaults
        return mergedSettings;
    }

    /// <summary>
    /// Saves settings data to the source that last contributed this section.
    /// </summary>
    /// <typeparam name="TSettings">The settings type.</typeparam>
    /// <param name="sectionName">The section name to use when saving.</param>
    /// <param name="settings">The settings data to save.</param>
    /// <param name="sectionMetadata">Metadata for the settings section.</param>
    /// <param name="cancellationToken">Cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    internal async Task SaveSettingsAsync<TSettings>(
        string sectionName,
        TSettings settings,
        SettingsSectionMetadata sectionMetadata,
        CancellationToken cancellationToken = default)
        where TSettings : class
    {
        this.EnsureReadyForOperations();
        this.LogSaveSettingsRequested(sectionName);
        var targetSourceId = this.ResolveSaveTargetSourceId(sectionName);

        // Get source reference under lock to prevent concurrent removal
        ISettingsSource targetSource;
        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            targetSource = this.GetSourceByIdOrThrow(targetSourceId);
        }

        var sectionsData = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            [sectionName] = settings,
        };

        var sectionsMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            [sectionName] = sectionMetadata,
        };

        try
        {
            await this.ExecuteWithSuppressedSourceChangeAsync(
                targetSourceId,
                async () =>
                {
                    // Increment version and create source metadata within the save operation
                    // Lock on the source to prevent concurrent version increments
                    SettingsSourceMetadata sourceMetadata;
                    lock (targetSource)
                    {
                        var newVersion = (targetSource.SourceMetadata?.Version ?? 0) + 1;
                        sourceMetadata = new SettingsSourceMetadata
                        {
                            WrittenAt = DateTimeOffset.UtcNow,
                            WrittenBy = "DroidNet.Config", // TODO: Make this configurable
                        };
                        sourceMetadata.SetVersion(newVersion, this.metadataSetterKey);
                        targetSource.SourceMetadata = sourceMetadata;
                    }

                    var result = await targetSource.SaveAsync(sectionsData, sectionsMetadata, sourceMetadata, cancellationToken).ConfigureAwait(false);
                    if (!result.IsSuccess)
                    {
                        this.LogFailedToSaveSettingsForType(sectionName, targetSource.Id, result.Error.Message);
                        throw new SettingsPersistenceException(
                            $"Failed to save settings to source '{targetSource.Id}': {result.Error.Message}",
                            targetSource.Id);
                    }

                    this.CacheSection(sectionName, settings!, targetSourceId);
                    this.LogSavedSettingsForType(sectionName, targetSource.Id);

                    // Relay the event to external listeners, but not to our internal handler
                    this.OnSourceChanged(new SourceChangedEventArgs(targetSource.Id, SourceChangeType.Updated));
                }).ConfigureAwait(false);
        }
        catch (SettingsPersistenceException)
        {
            throw;
        }
        catch (Exception ex)
        {
            this.LogExceptionSavingSettingsForType(ex, sectionName, targetSource.Id);
            throw new SettingsPersistenceException(
                $"Failed to save settings to source '{targetSource.Id}'",
                targetSource.Id,
                ex);
        }
    }

    private void ApplySettings(ISettingsService service)
    {
        var sectionName = service.SectionName;
        this.LogApplyingSettings(sectionName);

        if (this.cachedSections.ContainsKey(sectionName))
        {
            var materialized = this.GetMaterializedSectionData(sectionName, service.SettingsType);
            if (materialized is not null)
            {
                this.LogApplyingCachedSection(sectionName, materialized.GetType().Name);
            }
            else
            {
                this.LogApplyingCachedSection(sectionName, "null");
            }

            service.ApplyProperties(materialized);
            this.LogLoadedSettingsForType(sectionName, "cache");
        }
        else
        {
            this.LogNoCachedData(sectionName);
        }
    }

    private void OnSourceChanged(SourceChangedEventArgs args)
        => this.SourceChanged?.Invoke(this, args);

    private void SubscribeToSourceEvents(ISettingsSource source)
    {
        this.LogSubscribingToSource(source.Id);
        source.SourceChanged += this.OnSourceChangedInternal;
    }

    private void UnsubscribeFromSourceEvents(ISettingsSource source)
    {
        this.LogUnsubscribingFromSource(source.Id);
        source.SourceChanged -= this.OnSourceChangedInternal;
    }

    private void OnSourceChangedInternal(object? sender, SourceChangedEventArgs args)
    {
        this.LogSourceChangedEventReceived(args.SourceId, args.ChangeType);

        // Handle source changes asynchronously without blocking the event handler
        _ = Task.Run(async () =>
        {
            try
            {
                await this.HandleSourceChangedAsync(args, CancellationToken.None).ConfigureAwait(false);
            }
#pragma warning disable CA1031 // Do not catch general exception types - fire-and-forget handler
            catch (Exception ex)
#pragma warning restore CA1031
            {
                this.LogErrorHandlingSourceChange(ex, args.SourceId);
            }
        });
    }

    private void RemoveSectionsNoLongerProvided(ISettingsSource source, IReadOnlyDictionary<string, object> latestSections)
    {
        var ownedSections = this.GetSectionsOwnedBySource(source.Id);
        foreach (var sectionName in ownedSections)
        {
            if (latestSections.ContainsKey(sectionName))
            {
                continue;
            }

            this.LogRemovingStaleSection(sectionName, source.Id);
            _ = this.cachedSections.TryRemove(sectionName, out _);
        }
    }

    private bool ShouldUpdateCacheFor(string sectionName, string sourceId, bool allowReload)
    {
        if (!allowReload)
        {
            return true;
        }

        if (!this.cachedSections.TryGetValue(sectionName, out var cached))
        {
            return true;
        }

        return string.Equals(cached.SourceId, sourceId, StringComparison.Ordinal);
    }

    private void CacheSection(string sectionName, object sectionData, string sourceId)
        => this.cachedSections[sectionName] = new CachedSection(sectionData, sourceId);

    private List<string> GetSectionsOwnedBySource(string sourceId)
        => this.cachedSections
            .Where(kvp => string.Equals(kvp.Value.SourceId, sourceId, StringComparison.Ordinal))
            .Select(kvp => kvp.Key)
            .ToList();

    private object? GetMaterializedSectionData(string sectionName, Type targetType)
    {
        if (!this.cachedSections.TryGetValue(sectionName, out var cached))
        {
            return null;
        }

        var materialized = ConvertSectionData(cached.Data, targetType);
        if (!ReferenceEquals(materialized, cached.Data))
        {
            cached.Data = materialized;
        }

        return materialized;
    }

    private static object? ConvertSectionData(object? data, Type targetType)
    {
        ArgumentNullException.ThrowIfNull(targetType);

        if (data is null)
        {
            return null;
        }

        if (targetType.IsInstanceOfType(data))
        {
            return data;
        }

        if (data is JsonElement jsonElement)
        {
            return JsonSerializer.Deserialize(jsonElement.GetRawText(), targetType, JsonOptions);
        }

        return data;
    }

    private async Task HandleSourceChangedAsync(
        SourceChangedEventArgs args,
        CancellationToken cancellationToken)
    {
        // Check if we should suppress handling this change (e.g., during save operations)
        if (this.suppressChangeNotificationFor.ContainsKey(args.SourceId))
        {
            this.LogSuppressingChangeNotification(args.SourceId);
            return;
        }

        this.LogHandlingSourceChange(args.SourceId, args.ChangeType);
        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            var source = this.sources.FirstOrDefault(
                s => string.Equals(s.Id, args.SourceId, StringComparison.Ordinal));

            if (source is null)
            {
                this.LogSourceNotFound(args.SourceId);
                return; // Source was removed
            }

            // Track which sections this source owned before reload (i.e., was the winner for)
            var sectionsBeforeReload = this.GetSectionsOwnedBySource(args.SourceId)
                .ToHashSet(StringComparer.Ordinal);

            // Reload the source - this will update cache/map for sections this source provides
            // BUT it will only update cache for sections where this source is the winner
            // (TryLoadSourceAsync now respects last-loaded-wins)
            await this.TryLoadSourceAsync(source, allowReload: true, cancellationToken).ConfigureAwait(false);

            // Find sections this source owns after reload (i.e., is the winner for)
            var sectionsAfterReload = this.GetSectionsOwnedBySource(args.SourceId)
                .ToHashSet(StringComparer.Ordinal);

            // Only affect sections that this source is the WINNER for (owns in the map)
            // This respects last-loaded-wins: if another source later loaded the same section,
            // this source's change won't override it
            var affectedSections = sectionsBeforeReload.Union(sectionsAfterReload).ToList();
            this.LogSourceReloadSummary(args.SourceId, sectionsBeforeReload.Count, sectionsAfterReload.Count, affectedSections.Count);

            if (affectedSections.Count > 0)
            {
                var serviceUpdates = this.ServiceUpdates;
                var impactedServices = serviceUpdates.GetImpactedServices(affectedSections);
                this.LogReloadingImpactedServices(impactedServices.Count, affectedSections.Count);
                serviceUpdates.UpdateServices(impactedServices);
            }
        }
    }

    private async Task TryLoadSourceAsync(ISettingsSource source, bool allowReload, CancellationToken cancellationToken)
    {
        this.LogLoadingSource(source.Id);
        var loadResult = await source.LoadAsync(reload: allowReload, cancellationToken).ConfigureAwait(false);
        _ = loadResult.Tap(
            payload =>
            {
                // Initialize the source metadata from loaded data
                if (payload.SourceMetadata != null)
                {
                    source.SourceMetadata = payload.SourceMetadata;
                }

                if (allowReload)
                {
                    this.RemoveSectionsNoLongerProvided(source, payload.Sections);
                }

                foreach (var (sectionName, sectionData) in payload.Sections)
                {
                    // On success, neither the section name nor data should be null
                    Debug.Assert(sectionName is { Length: > 0 }, "Section name should not be null or empty.");
                    Debug.Assert(sectionData is { }, "Section data should not be null.");

                    var shouldUpdate = this.ShouldUpdateCacheFor(sectionName, source.Id, allowReload);

                    if (shouldUpdate)
                    {
                        this.CacheSection(sectionName, sectionData, source.Id);
                        this.LogSectionCached(sectionName, source.Id);
                    }
                    else
                    {
                        var currentOwnerLabel = this.cachedSections.TryGetValue(sectionName, out var cached)
                            ? cached.SourceId
                            : null;
                        this.LogSectionCacheSkipped(sectionName, source.Id, currentOwnerLabel);
                    }
                }

                this.LogLoadedSource(source.Id);
                this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Added));
            },
            err => this.LogFailedToLoadSource(source.Id, err.Message));
    }

    private void NotifyServicesOfReload()
    {
        // After reloading all sources, update all registered services with the latest cached data
        this.ServiceUpdates.NotifyAllServices();
    }

    private async Task LoadAllSourcesAsync(bool allowReload, CancellationToken cancellationToken)
    {
        foreach (var source in this.sources)
        {
            await this.TryLoadSourceAsync(source, allowReload, cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task ExecuteWithSuppressedSourceChangeAsync(string sourceId, Func<Task> action)
    {
        _ = this.suppressChangeNotificationFor.TryAdd(sourceId, 0);
        try
        {
            await action().ConfigureAwait(false);
        }
        finally
        {
            _ = this.suppressChangeNotificationFor.TryRemove(sourceId, out _);
        }
    }

    private string ResolveSaveTargetSourceId(string sectionName)
    {
        if (this.cachedSections.TryGetValue(sectionName, out var cached))
        {
            this.LogSavingToWinningSource(sectionName, cached.SourceId);
            return cached.SourceId;
        }

        var fallbackSourceId = this.sources.FirstOrDefault()?.Id;
        if (fallbackSourceId is null)
        {
            throw new InvalidOperationException("No sources available to save settings.");
        }

        this.LogSavingToFirstAvailableSource(sectionName, fallbackSourceId);
        return fallbackSourceId;
    }

    private ISettingsSource GetSourceByIdOrThrow(string sourceId)
    {
        var targetSource = this.sources.FirstOrDefault(s => string.Equals(s.Id, sourceId, StringComparison.Ordinal));
        if (targetSource is null)
        {
            throw new InvalidOperationException($"Winning source '{sourceId}' not found in sources list.");
        }

        return targetSource;
    }

    private int FindSourceIndexById(string sourceId)
        => this.sources.FindIndex(s => string.Equals(s.Id, sourceId, StringComparison.Ordinal));

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, nameof(SettingsManager));

    private void ThrowIfNotInitialized()
    {
        if (!this.isInitialized)
        {
            throw new InvalidOperationException("SettingsManager must be initialized.");
        }
    }

    private void EnsureReadyForOperations()
    {
        this.ThrowIfDisposed();
        this.ThrowIfNotInitialized();
    }

    private sealed class ServiceUpdateCoordinator
    {
        private readonly SettingsManager owner;
        private readonly ConcurrentDictionary<Type, ISettingsService> serviceInstances;
        private readonly ConcurrentDictionary<string, CachedSection> cachedSections;

        public ServiceUpdateCoordinator(
            SettingsManager owner,
            ConcurrentDictionary<Type, ISettingsService> serviceInstances,
            ConcurrentDictionary<string, CachedSection> cachedSections)
        {
            this.owner = owner;
            this.serviceInstances = serviceInstances;
            this.cachedSections = cachedSections;
        }

        public List<ISettingsService> GetImpactedServices(IReadOnlyCollection<string> sectionNames)
        {
            var impactedServices = new List<ISettingsService>();

            foreach (var serviceInstance in this.serviceInstances.Values)
            {
                if (sectionNames.Contains(serviceInstance.SectionName, StringComparer.Ordinal))
                {
                    impactedServices.Add(serviceInstance);
                }
            }

            return impactedServices;
        }

        public void UpdateServices(List<ISettingsService> impactedServices)
        {
            this.owner.LogUpdatingImpactedServices(impactedServices.Count);

            foreach (var serviceInstance in impactedServices)
            {
                var serviceType = serviceInstance.GetType();
                var sectionName = serviceInstance.SectionName;

                if (this.cachedSections.ContainsKey(sectionName))
                {
                    this.owner.LogServiceCacheHit(serviceType.Name, sectionName);
                    var materialized = this.owner.GetMaterializedSectionData(sectionName, serviceInstance.SettingsType);
                    serviceInstance.ApplyProperties(materialized);
                    this.owner.LogUpdatedService(serviceType.Name);
                }
                else
                {
                    serviceInstance.ApplyProperties(null);
                    this.owner.LogUpdatedService(serviceType.Name);
                    this.owner.LogServiceResetToDefaults(serviceType.Name, sectionName);
                }
            }
        }

        public void NotifyAllServices()
        {
            var allServices = this.serviceInstances.Values.ToList();
            this.UpdateServices(allServices);
        }
    }

    private sealed class CachedSection
    {
        public CachedSection(object? data, string sourceId)
        {
            this.Data = data;
            this.SourceId = sourceId;
        }

        public object? Data { get; set; }

        public string SourceId { get; }
    }
}

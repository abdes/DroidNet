// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.Json;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Config;

/// <summary>
///     Provides a last-loaded-wins implementation of <see cref="ISettingsManager"/> for multi-source settings composition.
/// </summary>
/// <remarks>
///     This class manages multiple <see cref="ISettingsSource"/> instances, loading them in the order added. When multiple
///     sources provide the same section, the last-loaded source wins for that section.
///     <para>
///     Supports runtime reloading, addition, and removal of sources via <see cref="AddSourceAsync"/>, <see
///     cref="RemoveSourceAsync"/>, and <see cref="ReloadAllAsync"/>. These operations propagate updates to all active
///     settings services, but it is the responsibility of each service implementation to respond to changes.
///     </para>
///     <para>
///     Subscribe to the <see cref="SourceChanged"/> event to receive notifications about lifecycle changes to managed
///     sources.
///     </para>
/// </remarks>
/// <note>
///     When a source is configured, it remains configured until explicitly removed, even if it fails to load. This allows
///     retrying on subsequent reloads or when the source data changes.
/// </note>
/// <param name="sources">The collection of settings sources to manage.</param>
/// <param name="resolver">The dependency injection resolver for creating service instances.</param>
/// <param name="loggerFactory">
///     Optional logger factory for diagnostic output. If <see langword="null"/>, a no-op logger is used.
/// </param>
/// <exception cref="System.ArgumentNullException">
///     Thrown when <paramref name="sources"/> or <paramref name="resolver"/> is <see langword="null"/>.
/// </exception>
public sealed partial class SettingsManager(
    IEnumerable<ISettingsSource> sources,
    IResolver resolver,
    ILoggerFactory? loggerFactory = null) : ISettingsManager
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        Converters =
        {
            // Accept enum names as strings during deserialization and disallow integer values to avoid
            // accidental numeric mapping. This ensures config files use readable enum names like "Dark".
            new System.Text.Json.Serialization.JsonStringEnumConverter(allowIntegerValues: false),
        },
    };

    private readonly ILogger<SettingsManager> logger = loggerFactory?.CreateLogger<SettingsManager>() ?? NullLogger<SettingsManager>.Instance;

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

    /// <inheritdoc/>
    public IReadOnlyList<ISettingsSource> Sources
    {
        get
        {
            this.ThrowIfDisposed();
            return this.sources.AsReadOnly();
        }
    }

    private ServiceUpdateCoordinator ServiceUpdates => new(
        this,
        this.serviceInstances,
        this.cachedSections);

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

            // Start AutoSave if it was enabled before initialization
            this.OnAutoSaveChanged();
        }
    }

    /// <inheritdoc/>
    public ISettingsService<TSettingsInterface> GetService<TSettingsInterface>()
        where TSettingsInterface : class
    {
        this.EnsureReadyForOperations();

        var settingsType = typeof(TSettingsInterface);
        this.LogGetServiceRequested(settingsType.Name);

        // Use GetOrAdd overload with factoryArgument to avoid closure
        var result = this.serviceInstances.GetOrAdd(
            settingsType,
            static (type, state) =>
            {
                var (owner, settingsTypeLocal) = state;
                owner.LogCreatingService(settingsTypeLocal.Name);

                // Get the concrete service instance from the resolver, keyed by the settings type
                var newService = owner.resolver.Resolve<ISettingsService<TSettingsInterface>>(serviceKey: "__uninitialized__");
                owner.ApplySettings(newService);

                owner.LogServiceRegistered(settingsTypeLocal.Name, owner.serviceInstances.Count);
                return newService;
            },
            (this, settingsType));

        // The cast is safe because we only ever add ISettingsService<TSettingsInterface> for this type
        var service = (ISettingsService<TSettingsInterface>)result;

        // If the service was already present, log returning existing service
        if (result is not null && !ReferenceEquals(result, this.serviceInstances[settingsType]))
        {
            this.LogReturningExistingService(settingsType.Name);
        }
        else
        {
            // Notify AutoSaver if a new service was added
            this.autoSaver?.OnServiceAdded(service);
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

        // Dispose AutoSaver first
        this.autoSaver?.Dispose();
        this.autoSaver = null;

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
    ///     Loads settings data for a specific settings type from all sources, applying the last-loaded-wins strategy.
    /// </summary>
    /// <typeparam name="TSettings">The settings type to load.</typeparam>
    /// <param name="sectionName">The section name to load from the cached data.</param>
    /// <param name="pocoType">
    ///     The concrete POCO type to deserialize to when data is stored as <see cref="System.Text.Json.JsonElement"/>.
    ///     If <see langword="null"/>, defaults to <c>typeof(TSettings)</c>.
    /// </param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>The merged settings data, or <see langword="null"/> if no data is found for the section.</returns>
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
    ///     Saves settings data to the source that last contributed the specified section.
    /// </summary>
    /// <typeparam name="TSettings">The settings type to save.</typeparam>
    /// <param name="sectionName">The section name to use when saving.</param>
    /// <param name="settings">The settings data to save.</param>
    /// <param name="sectionMetadata">Metadata for the settings section.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="System.Threading.Tasks.Task"/> representing the asynchronous operation.</returns>
    internal async Task SaveSettingsAsync<TSettings>(
        string sectionName,
        TSettings settings,
        SettingsSectionMetadata sectionMetadata,
        CancellationToken cancellationToken = default)
        where TSettings : class
    {
        this.EnsureReadyForOperations();
        this.LogSaveSettingsRequested(sectionName);

        var sectionsData = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            [sectionName] = settings,
        };

        var sectionsMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            [sectionName] = sectionMetadata,
        };

        await this.SaveSectionsToSourcesAsync(sectionsData, sectionsMetadata, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>
    ///     Saves multiple settings sections to their respective sources in batched operations.
    ///     Groups sections by target source for efficient I/O and lock management.
    /// </summary>
    /// <param name="sectionsData">A dictionary mapping section names to their data.</param>
    /// <param name="sectionsMetadata">A dictionary mapping section names to their metadata.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <returns>A <see cref="System.Threading.Tasks.Task"/> representing the asynchronous operation.</returns>
    /// <exception cref="SettingsPersistenceException">Thrown when saving to any source fails.</exception>
    internal async Task SaveSectionsToSourcesAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionsMetadata,
        CancellationToken cancellationToken = default)
    {
        if (sectionsData.Count == 0)
        {
            return;
        }

        // Group sections by target source (unlocked operation)
        var (sectionsBySource, metadataBySource) = this.GroupSectionsBySource(sectionsData, sectionsMetadata);

        // Acquire lock once for all save operations
        var releaser = await this.initializationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            await this.SaveGroupedSectionsLockedAsync(sectionsBySource, metadataBySource, cancellationToken).ConfigureAwait(false);
        }
    }

    private (
        Dictionary<string, Dictionary<string, object>> sectionsBySource,
        Dictionary<string, Dictionary<string, SettingsSectionMetadata>> metadataBySource)
        GroupSectionsBySource(
            IReadOnlyDictionary<string, object> sectionsData,
            IReadOnlyDictionary<string, SettingsSectionMetadata> sectionsMetadata)
    {
        var sectionsBySource = new Dictionary<string, Dictionary<string, object>>(StringComparer.Ordinal);
        var metadataBySource = new Dictionary<string, Dictionary<string, SettingsSectionMetadata>>(StringComparer.Ordinal);

        foreach (var (sectionName, sectionData) in sectionsData)
        {
            var targetSourceId = this.ResolveSaveTargetSourceId(sectionName);

            if (!sectionsBySource.TryGetValue(targetSourceId, out var value))
            {
                value = new Dictionary<string, object>(StringComparer.Ordinal);
                sectionsBySource[targetSourceId] = value;
                metadataBySource[targetSourceId] = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal);
            }

            value[sectionName] = sectionData;
            if (sectionsMetadata.TryGetValue(sectionName, out var metadata))
            {
                metadataBySource[targetSourceId][sectionName] = metadata;
            }
        }

        return (sectionsBySource, metadataBySource);
    }

    /// <summary>
    ///     Saves grouped sections to their sources. Must be called within the initialization lock.
    /// </summary>
    /// <param name="sectionsBySource">A dictionary mapping source IDs to the sections and their data to save to each source.</param>
    /// <param name="metadataBySource">A dictionary mapping source IDs to the corresponding sections' metadata.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    private async Task SaveGroupedSectionsLockedAsync(
        Dictionary<string, Dictionary<string, object>> sectionsBySource,
        Dictionary<string, Dictionary<string, SettingsSectionMetadata>> metadataBySource,
        CancellationToken cancellationToken)
    {
        // Save to each source with its batched sections
        foreach (var (sourceId, sourceSections) in sectionsBySource)
        {
            var sourceMetadata = metadataBySource[sourceId];
            await this.SaveToSourceLockedAsync(sourceId, sourceSections, sourceMetadata, cancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>
    ///     Saves sections to a specific source. Must be called within the initialization lock.
    /// </summary>
    /// <param name="sourceId">The ID of the target source to save sections to.</param>
    /// <param name="sectionsData">A dictionary mapping section names to the data to save for that source.</param>
    /// <param name="sectionsMetadata">A dictionary mapping section names to their metadata for that source.</param>
    /// <param name="cancellationToken">A token to monitor for cancellation requests.</param>
    /// <exception cref="SettingsPersistenceException">Thrown when saving to the target source fails.</exception>
    private async Task SaveToSourceLockedAsync(
        string sourceId,
        Dictionary<string, object> sectionsData,
        Dictionary<string, SettingsSectionMetadata> sectionsMetadata,
        CancellationToken cancellationToken)
    {
        var targetSource = this.GetSourceByIdOrThrow(sourceId);

        try
        {
            await this.ExecuteWithSuppressedSourceChangeAsync(
                sourceId,
                async () =>
                {
                    var sourceMetadata = this.UpdateSourceVersion(targetSource);

                    var result = await targetSource.SaveAsync(sectionsData, sectionsMetadata, sourceMetadata, cancellationToken).ConfigureAwait(false);
                    if (!result.IsSuccess)
                    {
                        var sectionNames = string.Join(", ", sectionsData.Keys);
                        this.LogFailedToSaveSettingsForType(sectionNames, targetSource.Id, result.Error.Message);
                        throw new SettingsPersistenceException(
                            $"Failed to save settings to source '{targetSource.Id}': {result.Error.Message}",
                            targetSource.Id);
                    }

                    // Cache all saved sections
                    foreach (var (sectionName, sectionData) in sectionsData)
                    {
                        this.CacheSection(sectionName, sectionData, sourceId);
                        this.LogSavedSettingsForType(sectionName, targetSource.Id);
                    }

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
            var sectionNames = string.Join(", ", sectionsData.Keys);
            this.LogExceptionSavingSettingsForType(ex, sectionNames, targetSource.Id);
            throw new SettingsPersistenceException(
                $"Failed to save settings to source '{targetSource.Id}'",
                targetSource.Id,
                ex);
        }
    }

    /// <summary>
    ///     Updates the source version and metadata. Must be called under the source lock.
    /// </summary>
    /// <param name="source">The source whose version and metadata will be updated.</param>
    /// <returns>The new <see cref="SettingsSourceMetadata"/> instance set on the source.</returns>
    private SettingsSourceMetadata UpdateSourceVersion(ISettingsSource source)
    {
        // Increment version and create source metadata within the save operation
        // Lock on the source to prevent concurrent version increments
        lock (source)
        {
            var newVersion = (source.SourceMetadata?.Version ?? 0) + 1;
            var meta = new SettingsSourceMetadata
            {
                WrittenAt = DateTimeOffset.UtcNow,
                WrittenBy = "DroidNet.Config", // TODO: Make this configurable
            };

            meta.SetVersion(newVersion, this.metadataSetterKey);
            source.SourceMetadata = meta;
            return meta;
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
        => !allowReload
            || !this.cachedSections.TryGetValue(sectionName, out var cached)
            || string.Equals(cached.SourceId, sourceId, StringComparison.Ordinal);

    private void CacheSection(string sectionName, object sectionData, string sourceId)
        => this.cachedSections[sectionName] = new CachedSection(sectionData, sourceId);

    private List<string> GetSectionsOwnedBySource(string sourceId)
        => [..
            this.cachedSections
                .Where(kvp => string.Equals(kvp.Value.SourceId, sourceId, StringComparison.Ordinal))
                .Select(kvp => kvp.Key),
        ];

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

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
        static object? ConvertSectionData(object? data, Type targetType)
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
            await this.HandleSourceChangedLockedAsync(args, cancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Handles source changed event. Must be called within the initialization lock.
    /// </summary>
    private async Task HandleSourceChangedLockedAsync(
        SourceChangedEventArgs args,
        CancellationToken cancellationToken)
    {
        var source = this.sources.FirstOrDefault(
            s => string.Equals(s.Id, args.SourceId, StringComparison.Ordinal));

        if (source is null)
        {
            this.LogSourceNotFound(args.SourceId);
            return; // Source was removed
        }

        // Track which sections this source owned before reload
        var sectionsBeforeReload = this.GetSectionsOwnedBySource(args.SourceId)
            .ToHashSet(StringComparer.Ordinal);

        // Reload the source - respects last-loaded-wins
        await this.TryLoadSourceAsync(source, allowReload: true, cancellationToken).ConfigureAwait(false);

        // Find sections this source owns after reload
        var sectionsAfterReload = this.GetSectionsOwnedBySource(args.SourceId)
            .ToHashSet(StringComparer.Ordinal);

        // Only affect sections that this source is the WINNER for
        var affectedSections = sectionsBeforeReload.Union(sectionsAfterReload, StringComparer.Ordinal).ToList();
        this.LogSourceReloadSummary(args.SourceId, sectionsBeforeReload.Count, sectionsAfterReload.Count, affectedSections.Count);

        if (affectedSections.Count > 0)
        {
            this.UpdateImpactedServices(affectedSections);
        }
    }

    private void UpdateImpactedServices(List<string> affectedSections)
    {
        var serviceUpdates = this.ServiceUpdates;
        var impactedServices = serviceUpdates.GetImpactedServices(affectedSections);
        this.LogReloadingImpactedServices(impactedServices.Count, affectedSections.Count);
        serviceUpdates.UpdateServices(impactedServices);
    }

    private async Task TryLoadSourceAsync(ISettingsSource source, bool allowReload, CancellationToken cancellationToken)
    {
        this.LogLoadingSource(source.Id);
        var loadResult = await source.LoadAsync(reload: allowReload, cancellationToken).ConfigureAwait(false);

        _ = loadResult.Tap(
            ProcessLoadedPayload,
            err => this.LogFailedToLoadSource(source.Id, err.Message));

        void ProcessLoadedPayload(SettingsReadPayload payload)
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

            CacheSections(payload.Sections);

            this.LogLoadedSource(source.Id);
            this.OnSourceChanged(new SourceChangedEventArgs(source.Id, SourceChangeType.Added));
        }

        void CacheSections(IReadOnlyDictionary<string, object> sections)
        {
            foreach (var (sectionName, sectionData) in sections)
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
        }
    }

    // After reloading all sources, update all registered services with the latest cached data
    private void NotifyServicesOfReload() => this.ServiceUpdates.NotifyAllServices();

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

        var fallbackSourceId = this.sources.FirstOrDefault()?.Id
            ?? throw new InvalidOperationException("No sources available to save settings.");
        this.LogSavingToFirstAvailableSource(sectionName, fallbackSourceId);
        return fallbackSourceId;
    }

    private ISettingsSource GetSourceByIdOrThrow(string sourceId)
        => this.sources.FirstOrDefault(s => string.Equals(s.Id, sourceId, StringComparison.Ordinal))
            ?? throw new InvalidOperationException($"Winning source '{sourceId}' not found in sources list.");

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

    private sealed class ServiceUpdateCoordinator(
        SettingsManager owner,
        ConcurrentDictionary<Type, ISettingsService> serviceInstances,
        ConcurrentDictionary<string, SettingsManager.CachedSection> cachedSections)
    {
        private readonly SettingsManager owner = owner;
        private readonly ConcurrentDictionary<Type, ISettingsService> serviceInstances = serviceInstances;
        private readonly ConcurrentDictionary<string, CachedSection> cachedSections = cachedSections;

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
                    serviceInstance.ApplyProperties(data: null);
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

    private sealed class CachedSection(object? data, string sourceId)
    {
        public object? Data { get; set; } = data;

        public string SourceId { get; } = sourceId;
    }
}

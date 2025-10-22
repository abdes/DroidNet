// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Central orchestrator that manages all settings sources and settings services.
/// </summary>
/// <remarks>
///     The settings manager must be initialized by calling <see cref="InitializeAsync(CancellationToken)"/> before any
///     settings services can be retrieved via <see cref="GetService{TSettings}"/>. This ensures that all sources are
///     loaded and ready, all sections are discovered, and services can be properly mapped to their corresponding
///     sections loaded from the source.
/// </remarks>
public interface ISettingsManager : IDisposable
{
    /// <summary>
    ///     Event raised when a settings source <see cref="SourceChangedEventArgs">lifecycle change</see> occurs.
    /// </summary>
    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    /// <summary>
    ///     Gets the collection of all settings sources, currently managed by the settings manager. This collection
    ///     includes sources registered with the DI container, or added at runtime directly to the manager.
    /// </summary>
    /// <remarks>
    ///     The order of sources in this collection reflects the order in which they were added to the manager, and is
    ///     crucial in applying the last-loaded-wins strategy for settings composition.
    /// </remarks>
    public IReadOnlyList<ISettingsSource> Sources { get; }

    /// <summary>
    ///     Gets or sets a value indicating whether automatic saving of dirty settings is enabled.
    ///     When enabled, dirty services will be automatically saved after <see cref="AutoSaveDelay"/> with no further changes.
    ///     Toggling from ON to OFF will immediately save all dirty services before stopping.
    /// </summary>
    public bool AutoSave { get; set; }

    /// <summary>
    ///     Gets or sets the delay period for auto-save debouncing.
    ///     Changes take effect on the next dirty notification.
    /// </summary>
    public TimeSpan AutoSaveDelay { get; set; }

    /// <summary>
    ///     Initializes the settings manager during application startup, by loading all settings sources in the current
    ///     snapshot of the <see cref="Sources"/> collection. This method must be called before any settings services
    ///     can be retrieved, and will only have an effect the first time it is invoked.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the initialization.</param>
    /// <returns>A task that represents the initialization operation.</returns>
    public Task InitializeAsync(CancellationToken cancellationToken = default);

    /// <summary>
    ///     Gets or creates a settings service for the specified settings type. It is up to the concrete implementation
    ///     to match the settings type to the appropriate section loaded from the sources. If no matching section is found,
    ///     a valid service instance should still be returned, with default values for all settings.
    /// </summary>
    /// <typeparam name="TSettingsInterface">The settings type interface.</typeparam>
    /// <returns>The settings service instance.</returns>
    public ISettingsService<TSettingsInterface> GetService<TSettingsInterface>()
        where TSettingsInterface : class;

    /// <summary>
    ///     Reloads all settings sources and updates all active settings services with data from the refreshed sources,
    ///     or default values if sections are missing after reload.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the reload operation.</param>
    /// <returns>A task that represents the reload operation.</returns>
    public Task ReloadAllAsync(CancellationToken cancellationToken = default);

    /// <summary>
    ///     Adds a new settings source to the manager. Concrete implementations may have different strategies for
    ///     resolving conflicts and applying changes to active settings services.
    /// </summary>
    /// <param name="source">The settings source to add.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>A task that represents the add operation.</returns>
    public Task AddSourceAsync(ISettingsSource source, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Removes a settings source from the manager. Concrete implementations may have different strategies for
    ///     applying changes to active settings services.
    /// </summary>
    /// <param name="sourceId">The identifier of the source to remove.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>A task that represents the remove operation.</returns>
    public Task RemoveSourceAsync(string sourceId, CancellationToken cancellationToken = default);
}

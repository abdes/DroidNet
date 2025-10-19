// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Central manager for all settings sources and services.
/// Orchestrates source loading with last-loaded-wins strategy and provides typed settings service instances.
/// </summary>
public interface ISettingsManager : IDisposable
{
    /// <summary>
    /// Event raised when a settings source lifecycle change occurs (added, updated, removed, failed).
    /// </summary>
    public event EventHandler<SettingsSourceChangedEventArgs>? SourceChanged;

    /// <summary>
    /// Gets the collection of all registered settings sources.
    /// </summary>
    public IReadOnlyList<ISettingsSource> Sources { get; }

    /// <summary>
    /// Initializes all registered settings sources.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the initialization.</param>
    /// <returns>A task that represents the initialization operation.</returns>
    public Task InitializeAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Gets or creates a settings service for the specified settings type.
    /// </summary>
    /// <typeparam name="TSettings">The settings type interface.</typeparam>
    /// <returns>The settings service instance.</returns>
    public ISettingsService<TSettings> GetService<TSettings>()
        where TSettings : class;

    /// <summary>
    /// Reloads all settings sources and updates all active settings services.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the reload operation.</param>
    /// <returns>A task that represents the reload operation.</returns>
    public Task ReloadAllAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Runs migrations for all registered settings services.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the migration operation.</param>
    /// <returns>A task that represents the migration operation.</returns>
    public Task RunMigrationsAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Adds a new settings source to the manager.
    /// </summary>
    /// <param name="source">The settings source to add.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>A task that represents the add operation.</returns>
    public Task AddSourceAsync(ISettingsSource source, CancellationToken cancellationToken = default);

    /// <summary>
    /// Removes a settings source from the manager.
    /// </summary>
    /// <param name="sourceId">The identifier of the source to remove.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>A task that represents the remove operation.</returns>
    public Task RemoveSourceAsync(string sourceId, CancellationToken cancellationToken = default);

    /// <summary>
    /// Subscribes to source change notifications.
    /// </summary>
    /// <param name="handler">The handler to invoke when source changes occur.</param>
    /// <returns>A disposable object that unsubscribes when disposed.</returns>
    public IDisposable SubscribeToSourceChanges(Action<SettingsSourceChangedEventArgs> handler);
}

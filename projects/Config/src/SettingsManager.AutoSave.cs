// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.ComponentModel;

namespace DroidNet.Config;

/// <summary>
///     Provides a last-loaded-wins implementation of <see cref="ISettingsManager"/> for multi-source settings composition.
/// </summary>
public sealed partial class SettingsManager
{
    private bool autoSave;
    private TimeSpan autoSaveDelay = TimeSpan.FromSeconds(2);
    private AutoSaver? autoSaver;

    /// <summary>
    /// Gets or sets a value indicating whether automatic saving of dirty settings is enabled.
    /// When enabled, dirty services will be automatically saved after <see cref="AutoSaveDelay"/> with no further changes.
    /// Toggling from ON to OFF will immediately save all dirty services before stopping.
    /// </summary>
    public bool AutoSave
    {
        get => this.autoSave;
        set
        {
            if (this.autoSave == value)
            {
                return;
            }

            this.autoSave = value;
            this.OnAutoSaveChanged();
        }
    }

    /// <summary>
    /// Gets or sets the delay period for auto-save debouncing.
    /// Changes take effect on the next dirty notification.
    /// </summary>
    public TimeSpan AutoSaveDelay
    {
        get => this.autoSaveDelay;
        set
        {
            if (value <= TimeSpan.Zero)
            {
                throw new ArgumentOutOfRangeException(nameof(value), "AutoSaveDelay must be positive.");
            }

            this.autoSaveDelay = value;
        }
    }

    private void OnAutoSaveChanged()
    {
        if (!this.isInitialized)
        {
            return; // Will be started after initialization if needed
        }

        if (this.autoSave)
        {
            // Start AutoSave
            this.autoSaver?.Dispose();
            this.autoSaver = new AutoSaver(this, this.serviceInstances);
            this.autoSaver.Start();
        }
        else
        {
            // Stop AutoSave (will save all dirty services immediately)
            if (this.autoSaver is not null)
            {
                _ = Task.Run(async () =>
                {
                    try
                    {
                        await this.autoSaver.StopAsync().ConfigureAwait(false);
                    }
#pragma warning disable CA1031 // Do not catch general exception types
                    catch (Exception ex)
#pragma warning restore CA1031
                    {
                        this.LogAutoSaveError(ex);
                    }
                    finally
                    {
                        this.autoSaver?.Dispose();
                        this.autoSaver = null;
                    }
                });
            }
        }
    }

    /// <summary>
    ///     Private helper class that manages automatic saving of dirty settings with debouncing.
    /// </summary>
    /// <param name="owner">Reference to the parent <see cref="SettingsManager"/> that owns this helper.</param>
    /// <param name="serviceInstances">Collection of registered settings services to monitor and save.</param>
    /// <exception cref="System.ArgumentNullException">Thrown when <paramref name="serviceInstances"/> is <c>null</c>.</exception>
    /// <remarks>
    ///     The <c>AutoSaver</c> subscribes to <see cref="INotifyPropertyChanged"/> notifications from
    ///     monitored services and debounces saves using <see cref="SettingsManager.AutoSaveDelay"/>. It
    ///     coalesces concurrent save requests, runs saves serially, and will execute any pending save that
    ///     occurred while a save was in progress.
    ///     <para>
    ///     Disposal blocks until any ongoing save operation finishes; disposal is safe to call multiple
    ///     times (the parent protects against double-disposal).
    ///     </para>
    /// </remarks>
    private sealed class AutoSaver(SettingsManager owner, ConcurrentDictionary<Type, ISettingsService> serviceInstances) : IDisposable
    {
        private readonly ConcurrentDictionary<Type, ISettingsService> serviceInstances = serviceInstances ?? throw new ArgumentNullException(nameof(serviceInstances));
        private readonly Lock timerLock = new();
        private readonly Lock saveOperationLock = new();
        private Timer? debounceTimer;
        private Task? currentSaveOperation;
        private bool hasPendingSave;
        private bool isDisposed;

        /// <summary>
        /// Starts monitoring all registered services for IsDirty changes.
        /// </summary>
        public void Start()
        {
            this.ThrowIfDisposed();

            foreach (var service in this.serviceInstances.Values)
            {
                this.SubscribeToService(service);
            }

            // If any service is already dirty when AutoSave starts, trigger a save immediately.
            var hasDirty = this.serviceInstances.Values.Any(s => s.IsDirty);
            if (hasDirty)
            {
                // Fire-and-forget: start the save operation respecting existing save locks.
                _ = Task.Run(async () =>
                {
                    try
                    {
                        await this.TriggerSaveOperationAsync().ConfigureAwait(false);
                    }
#pragma warning disable CA1031 // Do not catch general exception types
                    catch (Exception ex)
#pragma warning restore CA1031
                    {
                        owner.LogAutoSaveError(ex);
                    }
                });
            }

            owner.LogAutoSaveStarted();
        }

        /// <summary>
        /// Stops monitoring, immediately saves all dirty services, and cancels any pending operations.
        /// </summary>
        public async Task StopAsync()
        {
            this.ThrowIfDisposed();

            // Unsubscribe from all services
            foreach (var service in this.serviceInstances.Values)
            {
                this.UnsubscribeFromService(service);
            }

            // Cancel any pending timer
            this.CancelPendingTimer();

            // Save all dirty services immediately
            await this.SaveAllDirtyServicesAsync(CancellationToken.None).ConfigureAwait(false);

            owner.LogAutoSaveStopped();
        }

        /// <summary>
        /// Subscribes to a service when it's added to the manager.
        /// </summary>
        public void OnServiceAdded(ISettingsService service)
        {
            this.ThrowIfDisposed();
            this.SubscribeToService(service);
        }

        public void Dispose()
        {
            // This will never happen as the only disposal is from SettingsManager
            // which protects against double disposal
            if (this.isDisposed)
            {
                return;
            }

            this.isDisposed = true;

            // Unsubscribe from all services
            foreach (var service in this.serviceInstances.Values)
            {
                service.PropertyChanged -= this.OnServicePropertyChanged;
            }

            // Dispose timer
            this.CancelPendingTimer();

            // Wait for any ongoing save operation to complete
            // We don't cancel it - let it finish gracefully
            if (this.currentSaveOperation is not null)
            {
                try
                {
                    this.currentSaveOperation.GetAwaiter().GetResult();
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch
#pragma warning restore CA1031
                {
                    // Ignore errors during disposal
                }
            }
        }

        private void SubscribeToService(ISettingsService service)
        {
            service.PropertyChanged -= this.OnServicePropertyChanged; // Prevent duplicate subscriptions
            service.PropertyChanged += this.OnServicePropertyChanged;
        }

        private void UnsubscribeFromService(ISettingsService service)
            => service.PropertyChanged -= this.OnServicePropertyChanged;

        private void OnServicePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            this.ThrowIfDisposed();

            if (!string.Equals(e.PropertyName, nameof(ISettingsService.IsDirty), StringComparison.Ordinal))
            {
                return;
            }

            if (sender is ISettingsService service)
            {
                // If the change originated from a source reload or other suppressed source change,
                // ignore it so reloads do not trigger AutoSave.
                if (owner.cachedSections.TryGetValue(service.SectionName, out var cached)
                    && owner.suppressChangeNotificationFor.ContainsKey(cached.SourceId))
                {
                    return;
                }

                if (service.IsDirty)
                {
                    this.ScheduleDebouncedSave();
                }
            }
        }

        private void ScheduleDebouncedSave()
        {
            this.ThrowIfDisposed();

            lock (this.timerLock)
            {
                // Cancel existing timer
                this.debounceTimer?.Dispose();

                // Create new timer that fires once after the delay
                this.debounceTimer = new Timer(
                    _ => this.OnDebounceTimerElapsed(),
                    state: null,
                    dueTime: owner.AutoSaveDelay,
                    period: Timeout.InfiniteTimeSpan);

                owner.LogAutoSaveDebouncing(owner.AutoSaveDelay);
            }
        }

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "MA0040:Forward the CancellationToken parameter to methods that take one", Justification = "cancellation token not always available")]
        private void OnDebounceTimerElapsed()
        {
            this.ThrowIfDisposed();

            // Fire and forget - trigger save operation
            _ = Task.Run(async () =>
            {
                try
                {
                    await this.TriggerSaveOperationAsync().ConfigureAwait(false);
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch (Exception ex)
#pragma warning restore CA1031
                {
                    owner.LogAutoSaveError(ex);
                }
            });
        }

        private async Task TriggerSaveOperationAsync()
        {
            this.ThrowIfDisposed();

            if (owner.sources.Count == 0)
            {
                owner.LogSaveNoSources();
                return; // No sources to save to
            }

            lock (this.saveOperationLock)
            {
                // If a save operation is already running, mark as pending and return
                if (this.currentSaveOperation?.IsCompleted == false)
                {
                    this.hasPendingSave = true;
                    owner.LogAutoSavePending();
                    return;
                }

                // Start a new save operation
                this.hasPendingSave = false;
                this.currentSaveOperation = this.SaveAllDirtyServicesAsync(CancellationToken.None);
            }

            // Wait for the current operation to complete (outside the lock)
            try
            {
                await this.currentSaveOperation.ConfigureAwait(false);
            }
            finally
            {
                // After completion, check if there's a pending save to execute
                await this.ExecutePendingSaveIfNeededAsync().ConfigureAwait(false);
            }
        }

        private async Task ExecutePendingSaveIfNeededAsync()
        {
            while (true)
            {
                Task? saveTask;

                lock (this.saveOperationLock)
                {
                    if (!this.hasPendingSave)
                    {
                        this.currentSaveOperation = null;
                        return; // No pending save, we're done
                    }

                    // Start the pending save
                    this.hasPendingSave = false;
                    this.currentSaveOperation = this.SaveAllDirtyServicesAsync(CancellationToken.None);
                    saveTask = this.currentSaveOperation;
                }

                // Execute the save outside the lock
                try
                {
                    await saveTask.ConfigureAwait(false);
                }
#pragma warning disable CA1031 // Do not catch general exception types
                catch (Exception ex)
#pragma warning restore CA1031
                {
                    owner.LogAutoSaveError(ex);
                }

                // Loop to check if another save became pending while we were saving
            }
        }

        private async Task SaveAllDirtyServicesAsync(CancellationToken cancellationToken)
        {
            this.ThrowIfDisposed();

            try
            {
                // Collect dirty services
                var dirtyServices = this.CollectDirtyServices();
                if (dirtyServices.Count == 0)
                {
                    return;
                }

                owner.LogAutoSavingServices(dirtyServices.Count);

                // Call SaveAsync on each service - this will handle snapshot comparison
                foreach (var service in dirtyServices)
                {
                    try
                    {
                        await service.SaveAsync(cancellationToken).ConfigureAwait(false);
                        owner.LogAutoSavedService(service.SectionName);
                    }
                    catch (SettingsValidationException ex)
                    {
                        owner.LogAutoSaveServiceFailed(service.SectionName, ex);
                    }
                    catch (SettingsPersistenceException ex)
                    {
                        owner.LogAutoSaveServiceFailed(service.SectionName, ex);
                    }
                    catch (ObjectDisposedException ex)
                    {
                        owner.LogAutoSaveServiceFailed(service.SectionName, ex);
                    }
                }

                owner.LogAutoSaveCompleted(dirtyServices.Count);
            }
            catch (OperationCanceledException)
            {
                owner.LogAutoSaveCancelled();
                throw; // Re-throw to let caller handle
            }
        }

        private List<ISettingsService> CollectDirtyServices()
            => [.. this.serviceInstances.Values.Where(s => s.IsDirty && !s.IsBusy)];

        private void CancelPendingTimer()
        {
            lock (this.timerLock)
            {
                this.debounceTimer?.Dispose();
                this.debounceTimer = null;
            }
        }

        private void ThrowIfDisposed()
            => ObjectDisposedException.ThrowIf(this.isDisposed, nameof(AutoSaver));
    }
}

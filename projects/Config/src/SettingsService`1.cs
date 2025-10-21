// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Runtime.CompilerServices;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Config;

/// <summary>
/// Provides management, validation, and persistence for a specific settings type.
/// Handles loading, saving, resetting, and validation of settings, and tracks state changes.
/// </summary>
/// <typeparam name="TSettings">The type of settings managed by this service.</typeparam>
/// <remarks>
/// This is an abstract base class. Concrete implementations must:
/// <list type="bullet">
/// <item>Inherit from SettingsService&lt;TSettings&gt;</item>
/// <item>Implement the TSettings interface</item>
/// <item>Override <see cref="GetSettingsSnapshot"/> to provide the current settings as a POCO</item>
/// <item>Override <see cref="UpdateProperties"/> to update properties from loaded settings</item>
/// <item>Override <see cref="CreateDefaultSettings"/> to provide default settings instance</item>
/// </list>
/// </remarks>
/// <param name="manager">The settings manager used to load and save settings.</param>
/// <param name="loggerFactory">Optional logger factory used to create a logger for diagnostic output.</param>
public abstract partial class SettingsService<TSettings>(SettingsManager manager, ILoggerFactory? loggerFactory = null) : ISettingsService<TSettings>
    where TSettings : class
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<SettingsService<TSettings>>() ?? NullLogger<SettingsService<TSettings>>.Instance;
    private readonly WeakReference<SettingsManager> managerReference = new(manager ?? throw new ArgumentNullException(nameof(manager)));
    private readonly AsyncLock operationLock = new();

    private bool isDirty;
    private bool isBusy;
    private bool isDisposed;

    /// <inheritdoc/>
    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets the section name used to identify this settings type in storage sources.
    /// Must be implemented by derived classes to provide a unique identifier.
    /// </summary>
    public abstract string SectionName { get; }

    /// <summary>
    /// Gets the current settings instance.
    /// </summary>
    public TSettings Settings
    {
        get
        {
            this.ThrowIfDisposed();
            return (TSettings)(object)this;
        }
    }

    /// <summary>
    /// Gets a value indicating whether the settings have unsaved changes.
    /// </summary>
    public bool IsDirty
    {
        get => this.isDirty;
        internal set => this.SetField(ref this.isDirty, value);
    }

    /// <summary>
    /// Gets a value indicating whether an operation is in progress.
    /// </summary>
    public bool IsBusy
    {
        get => this.isBusy;
        private set => this.SetField(ref this.isBusy, value);
    }

    /// <inheritdoc/>
    public abstract Type SettingsType { get; }

    /// <summary>
    /// Gets helper to get the manager instance from the weak reference. Throws if the manager has been collected.
    /// </summary>
    private SettingsManager Manager => this.managerReference.TryGetTarget(out var m)
        ? m
        : throw new InvalidOperationException("SettingsManager reference is no longer available.");

    /// <summary>
    /// Validates and saves the current settings to all writable sources.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task SaveAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        if (!this.IsDirty)
        {
            return;
        }

        var releaser = await this.operationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            try
            {
                this.IsBusy = true;

                // Log save start
                this.LogSaving();

                var validationErrors = await this.ValidateAsync(cancellationToken).ConfigureAwait(false);
                if (validationErrors.Count > 0)
                {
                    // Log validation failure
                    this.LogValidationFailed(validationErrors.Count);
                    throw new SettingsValidationException("Settings validation failed", validationErrors);
                }

                var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = DateTime.UtcNow.ToString("yyyyMMdd", System.Globalization.CultureInfo.InvariantCulture) };
                var settingsSnapshot = this.GetSettingsSnapshot();

                try
                {
                    await this.Manager.SaveSettingsAsync(this.SectionName, settingsSnapshot, metadata, cancellationToken).ConfigureAwait(false);
                    this.IsDirty = false;

                    // Log successful save
                    this.LogSavedSettings();
                }
                catch (Exception ex)
                {
                    // Log and rethrow
                    this.LogExceptionSaving(ex);
                    throw;
                }
            }
            finally
            {
                this.IsBusy = false;
            }
        }
    }

    /// <summary>
    /// Reloads settings from all sources, discarding unsaved changes.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task ReloadAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        var releaser = await this.operationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            try
            {
                this.IsBusy = true;

                // Log reload
                this.LogReloading();

                // First, reload all sources in the manager to refresh the cache
                await this.Manager.ReloadAllAsync(cancellationToken).ConfigureAwait(false);

                // Then load the refreshed settings from the updated cache
                var loadedSettings = await this.Manager.LoadSettingsAsync<TSettings>(this.SectionName, this.SettingsType, cancellationToken).ConfigureAwait(false);

                var usedDefaults = false;
                if (loadedSettings == null)
                {
                    loadedSettings = this.CreateDefaultSettings();
                    usedDefaults = true;
                }

                this.UpdateProperties(loadedSettings);

                this.IsDirty = false;

                if (usedDefaults)
                {
                    this.LogUsingDefaults();
                }

                this.LogReloaded();

                this.OnPropertyChanged(nameof(this.Settings));
            }
            finally
            {
                this.IsBusy = false;
            }
        }
    }

    /// <summary>
    /// Validates the current settings instance using data annotations.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A read-only list of validation errors, or empty if valid.</returns>
    public async Task<IReadOnlyList<SettingsValidationError>> ValidateAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        await Task.CompletedTask.ConfigureAwait(false);

        var errors = new List<SettingsValidationError>();
        var settingsSnapshot = this.GetSettingsSnapshot();
        var validationContext = new ValidationContext(settingsSnapshot);
        var validationResults = new List<ValidationResult>();
        var isValid = Validator.TryValidateObject(settingsSnapshot, validationContext, validationResults, validateAllProperties: true);

        if (!isValid)
        {
            foreach (var result in validationResults)
            {
                var propertyName = result.MemberNames.FirstOrDefault() ?? "Unknown";
                var errorMessage = result.ErrorMessage ?? "Validation failed";
                errors.Add(new SettingsValidationError(propertyName, errorMessage));
            }
        }

        return errors.AsReadOnly();
    }

    /// <summary>
    /// Resets the settings to their default values.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task ResetToDefaultsAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        var releaser = await this.operationLock.AcquireAsync(cancellationToken).ConfigureAwait(false);
        await using (releaser.ConfigureAwait(false))
        {
            try
            {
                this.IsBusy = true;
                var defaultSettings = this.CreateDefaultSettings();

                this.UpdateProperties(defaultSettings);

                this.IsDirty = true;

                // Log reset
                this.LogResetToDefaults();

                this.OnPropertyChanged(nameof(this.Settings));
            }
            finally
            {
                this.IsBusy = false;
            }
        }
    }

    /// <summary>
    /// Releases all resources used by the service.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases managed and unmanaged resources.
    /// </summary>
    /// <param name="disposing">True if called from Dispose, false if called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            // Log disposing
            this.LogDisposingService();
            this.operationLock.Dispose();
        }

        this.isDisposed = true;
    }

    /// <summary>
    /// Creates a POCO snapshot of the current settings state for serialization.
    /// </summary>
    /// <returns>A POCO object representing the current settings.</returns>
    protected abstract object GetSettingsSnapshot();

    /// <summary>
    /// Updates the properties of this service from a loaded settings POCO.
    /// </summary>
    /// <param name="settings">The loaded settings POCO to apply.</param>
    protected abstract void UpdateProperties(TSettings settings);

    /// <summary>
    /// Creates a new instance with default settings values.
    /// </summary>
    /// <returns>A default settings instance.</returns>
    protected abstract TSettings CreateDefaultSettings();

    /// <summary>
    /// Sets a field and raises <see cref="PropertyChanged"/> if the value changes.
    /// </summary>
    /// <typeparam name="T">The field type.</typeparam>
    /// <param name="field">The field reference.</param>
    /// <param name="value">The new value.</param>
    /// <param name="propertyName">The property name.</param>
    /// <returns>True if the value changed; otherwise, false.</returns>
    protected bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        this.OnPropertyChanged(propertyName);

        // Mark dirty only when the property name refers to a property of TSettings
        if (!string.IsNullOrEmpty(propertyName) && typeof(TSettings).GetProperty(propertyName) is not null)
        {
            // Use the public IsDirty property so PropertyChanged is raised for it as well
            this.IsDirty = true;
        }

        return true;
    }

    /// <summary>
    /// Raises the <see cref="PropertyChanged"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    /// <summary>
    /// Throws if the service has been disposed.
    /// </summary>
    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, nameof(SettingsService<>));
}

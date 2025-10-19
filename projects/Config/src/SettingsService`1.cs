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
public abstract class SettingsService<TSettings> : ISettingsService<TSettings>
    where TSettings : class
{
    private readonly SettingsManager manager;
    private readonly ILogger logger;
    private readonly SemaphoreSlim operationLock;
    private bool isDirty;
    private bool isBusy;
    private bool isInitialized;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsService{TSettings}"/> class.
    /// </summary>
    /// <param name="manager">The settings manager used to load and save settings.</param>
    /// <param name="loggerFactory">Optional logger factory used to create a logger for diagnostic output.</param>
    protected SettingsService(SettingsManager manager, ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SettingsService<TSettings>>() ?? NullLogger<SettingsService<TSettings>>.Instance;

        this.manager = manager ?? throw new ArgumentNullException(nameof(manager));
        this.operationLock = new SemaphoreSlim(1, 1);
    }

    /// <inheritdoc/>
    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Occurs when the initialization state changes.
    /// </summary>
    public event EventHandler<InitializationStateChangedEventArgs>? InitializationStateChanged;

    /// <summary>
    /// Occurs when an error is reported by the settings source.
    /// </summary>
    public event EventHandler<SourceErrorEventArgs>? SourceError;

    /// <summary>
    /// Gets the section name used to identify this settings type in storage sources.
    /// Must be implemented by derived classes to provide a unique identifier.
    /// </summary>
    public abstract string SectionName { get; }

    /// <summary>
    /// Gets the concrete POCO type used for deserialization from storage.
    /// Override this property if TSettings is an interface to specify the concrete implementation type.
    /// </summary>
    /// <remarks>
    /// When TSettings is an interface, this property should return the concrete class type
    /// that implements the interface and can be deserialized from JSON.
    /// The default implementation returns typeof(TSettings).
    /// </remarks>
    protected virtual Type PocoType => typeof(TSettings);

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

    /// <summary>
    /// Loads settings from all sources and initializes the service.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        if (this.isInitialized)
        {
            return;
        }

        await this.operationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            this.IsBusy = true;
            var loadedSettings = await this.manager.LoadSettingsAsync<TSettings>(this.SectionName, this.PocoType, cancellationToken).ConfigureAwait(false);

            // If no settings were loaded from sources, use default settings
            if (loadedSettings == null)
            {
                loadedSettings = this.CreateDefaultSettings();
            }

            // Update properties from loaded settings
            this.UpdateProperties(loadedSettings);

            this.isInitialized = true;
            this.IsDirty = false;
            this.InitializationStateChanged?.Invoke(this, new InitializationStateChangedEventArgs(true));
        }
        finally
        {
            this.IsBusy = false;
            _ = this.operationLock.Release();
        }
    }

    /// <summary>
    /// Validates and saves the current settings to all writable sources.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task SaveAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        this.ThrowIfNotInitialized();
        if (!this.IsDirty)
        {
            return;
        }

        await this.operationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            this.IsBusy = true;
            var validationErrors = await this.ValidateAsync(cancellationToken).ConfigureAwait(false);
            if (validationErrors.Count > 0)
            {
                throw new SettingsValidationException("Settings validation failed", validationErrors);
            }

            var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = DateTime.UtcNow.ToString("yyyyMMdd", System.Globalization.CultureInfo.InvariantCulture) };
            var settingsSnapshot = this.GetSettingsSnapshot();
            await this.manager.SaveSettingsAsync(this.SectionName, settingsSnapshot, metadata, cancellationToken).ConfigureAwait(false);
            this.IsDirty = false;
        }
        finally
        {
            this.IsBusy = false;
            _ = this.operationLock.Release();
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
        this.ThrowIfNotInitialized();

        await this.operationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            this.IsBusy = true;

            // First, reload all sources in the manager to refresh the cache
            await this.manager.ReloadAllAsync(cancellationToken).ConfigureAwait(false);

            // Then load the refreshed settings from the updated cache
            var loadedSettings = await this.manager.LoadSettingsAsync<TSettings>(this.SectionName, this.PocoType, cancellationToken).ConfigureAwait(false);

            // If no settings were loaded from sources, use default settings
            if (loadedSettings == null)
            {
                loadedSettings = this.CreateDefaultSettings();
            }

            this.UpdateProperties(loadedSettings);

            this.IsDirty = false;
            this.OnPropertyChanged(nameof(this.Settings));
        }
        finally
        {
            this.IsBusy = false;
            _ = this.operationLock.Release();
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
    /// Runs migrations for the settings type. (No-op by default.)
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task RunMigrationsAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        this.ThrowIfNotInitialized();
        await Task.CompletedTask.ConfigureAwait(false);
    }

    /// <summary>
    /// Resets the settings to their default values.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task ResetToDefaultsAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();
        this.ThrowIfNotInitialized();

        await this.operationLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            this.IsBusy = true;
            var defaultSettings = this.CreateDefaultSettings();

            this.UpdateProperties(defaultSettings);

            this.IsDirty = true;
            this.OnPropertyChanged(nameof(this.Settings));
        }
        finally
        {
            this.IsBusy = false;
            _ = this.operationLock.Release();
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
            this.operationLock.Dispose();
        }

        this.isDisposed = true;
    }

    /// <summary>
    /// Raises the <see cref="PropertyChanged"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

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
        return true;
    }

    /// <summary>
    /// Throws if the service has been disposed.
    /// </summary>
    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, nameof(SettingsService<>));

    /// <summary>
    /// Throws if the service has not been initialized.
    /// </summary>
    private void ThrowIfNotInitialized()
    {
        if (!this.isInitialized)
        {
            throw new InvalidOperationException("SettingsService must be initialized. Call InitializeAsync first.");
        }
    }
}

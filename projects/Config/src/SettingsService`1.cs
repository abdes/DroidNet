// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
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
    private readonly AsyncLocal<int> operationScopeDepth = new();

    private bool isDirty;
    private bool isBusy;
    private bool isDisposed;
    private bool suppressDirtyTracking;

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
        internal set => this.SetDirtyInternal(value);
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

    /// <inheritdoc/>
    public abstract SettingsSectionMetadata SectionMetadata { get; set; }

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
        this.operationScopeDepth.Value++;
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

                var settingsSnapshot = this.GetSettingsSnapshot();

                try
                {
                    await this.Manager.SaveSettingsAsync(this.SectionName, settingsSnapshot, this.SectionMetadata, cancellationToken).ConfigureAwait(false);
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
                this.operationScopeDepth.Value--;
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
        this.operationScopeDepth.Value++;
        await using (releaser.ConfigureAwait(false))
        {
            try
            {
                this.IsBusy = true;
                this.ApplyDefaults();

                this.IsDirty = true;

                // Log reset
                this.LogResetToDefaults();

                this.OnPropertyChanged(nameof(this.Settings));
            }
            finally
            {
                this.operationScopeDepth.Value--;
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

    /// <inheritdoc/>
    public void ApplyProperties(object? data)
    {
        this.ThrowIfDisposed();

        // Avoid nested locking if we are already inside a locked operation
        if (this.operationScopeDepth.Value > 0)
        {
            this.ApplyPropertiesCore(data);
            return;
        }

        var releaserTask = this.operationLock.AcquireAsync(CancellationToken.None).AsTask();
        var releaser = releaserTask.GetAwaiter().GetResult();
        using (releaser)
        {
            try
            {
                this.operationScopeDepth.Value++;
                this.ApplyPropertiesCore(data);
            }
            finally
            {
                this.operationScopeDepth.Value--;
            }
        }
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
    /// Creates and applies default settings to this service.
    /// </summary>
    /// <remarks>
    /// This helper centralizes the common pattern of creating a default settings instance via
    /// <see cref="CreateDefaultSettings"/> and delegating to <see cref="UpdateProperties(TSettings)"/>.
    /// It does not change dirty-tracking behavior; callers decide whether the application should mark
    /// the service dirty or clean (typically via <see cref="DirtyTrackingScope"/>).
    /// </remarks>
    protected virtual void ApplyDefaults()
    {
        var defaultSettings = this.CreateDefaultSettings();
        this.UpdateProperties(defaultSettings);
    }

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

        if (!this.suppressDirtyTracking
            && !string.IsNullOrEmpty(propertyName)
            && typeof(TSettings).GetProperty(propertyName) is not null)
        {
            this.SetDirtyInternal(value: true);
        }

        return true;
    }

    private void SetDirtyInternal(bool value)
    {
        if (this.isDirty == value)
        {
            return;
        }

        this.isDirty = value;
        this.LogDirtyStateChanged(value);
        this.OnPropertyChanged(nameof(this.IsDirty));
    }

    /// <summary>
    /// Raises the <see cref="PropertyChanged"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    private void ApplyPropertiesCore(object? data)
    {
        using var suppression = new DirtyTrackingScope(this);

        if (data is null)
        {
            this.ApplyDefaults();
            suppression.MarkClean();
            return;
        }

        if (data is TSettings typedData)
        {
            this.UpdateProperties(typedData);
            suppression.MarkClean();
            return;
        }

        throw new ArgumentException(
            $"Unsupported data type: {data.GetType().Name}. Expected {typeof(TSettings).Name}.",
            nameof(data));
    }

    /// <summary>
    /// Throws if the service has been disposed.
    /// </summary>
    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.isDisposed, nameof(SettingsService<>));

    private sealed class DirtyTrackingScope : IDisposable
    {
        private readonly SettingsService<TSettings> owner;
        private readonly bool previousValue;
        private bool markClean;

        public DirtyTrackingScope(SettingsService<TSettings> owner)
        {
            this.owner = owner;
            this.previousValue = owner.suppressDirtyTracking;
            owner.suppressDirtyTracking = true;
        }

        public void MarkClean() => this.markClean = true;

        public void Dispose()
        {
            this.owner.suppressDirtyTracking = this.previousValue;

            if (this.markClean)
            {
                this.owner.SetDirtyInternal(value: false);
            }
        }
    }
}

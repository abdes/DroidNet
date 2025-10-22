// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace DroidNet.Config;

/// <summary>
/// Provides the non-generic contract used by the settings manager to coordinate services.
/// </summary>
public interface ISettingsService : INotifyPropertyChanged, IDisposable
{
    /// <summary>
    /// Gets the section name used to identify this settings type in storage sources.
    /// </summary>
    public string SectionName { get; }

    /// <summary>
    /// Gets the concrete POCO type used for deserialization from storage.
    /// </summary>
    public Type SettingsType { get; }

    /// <summary>
    /// Gets a value indicating whether the settings have been modified since the last save.
    /// </summary>
    public bool IsDirty { get; }

    /// <summary>
    /// Gets a value indicating whether the service is currently performing an async operation.
    /// </summary>
    public bool IsBusy { get; }

    /// <summary>
    /// Gets or sets the metadata for this settings section.
    /// </summary>
    /// <remarks>
    /// This metadata is fully owned and managed by the service implementation.
    /// It contains service-specific information like schema version and service identifier.
    /// </remarks>
    public SettingsSectionMetadata SectionMetadata { get; set; }

    /// <summary>
    /// Saves the current settings to persistent storage if modifications exist.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the save operation.</param>
    /// <returns>A task that represents the save operation.</returns>
    public Task SaveAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Validates the current settings and returns any validation errors.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the validation.</param>
    /// <returns>A task that represents the validation operation. The task result contains validation errors.</returns>
    public Task<IReadOnlyList<SettingsValidationError>> ValidateAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Resets the settings to their default values.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the reset operation.</param>
    /// <returns>A task that represents the reset operation.</returns>
    public Task ResetToDefaultsAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Applies properties from cached data (POCO or JsonElement) to this service's settings.
    /// Used by the SettingsManager to update the service when sources change.
    /// </summary>
    /// <param name="data">The cached data to apply. Can be null to reset to defaults.</param>
    public void ApplyProperties(object? data);
}

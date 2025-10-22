// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace DroidNet.Config;

/// <summary>
///     Provides the non-generic contract used by the settings manager to coordinate settings services.
///     Implementations expose metadata, state, and lifecycle methods that the manager uses to load,
///     validate, reset, and persist settings for a named section.
/// </summary>
public interface ISettingsService : INotifyPropertyChanged, IDisposable
{
    /// <summary>
    ///     Gets the section name used to identify this settings type in storage sources.
    /// </summary>
    public string SectionName { get; }

    /// <summary>
    ///     Gets the concrete POCO type used for deserialization from storage.
    /// </summary>
    public Type SettingsType { get; }

    /// <summary>
    ///     Gets a value indicating whether the settings have been modified since the last save.
    ///     When true, callers typically call <see cref="SaveAsync"/> to persist changes.
    /// </summary>
    public bool IsDirty { get; }

    /// <summary>
    ///     Gets a value indicating whether the service is currently performing an asynchronous operation.
    ///     This can be used by callers to avoid concurrent operations or to show progress UI.
    /// </summary>
    public bool IsBusy { get; }

    /// <summary>
    ///     Gets or sets the metadata for this settings section.
    /// </summary>
    /// <remarks>
    ///     This metadata is fully owned and managed by the service implementation.
    ///     It contains service-specific information such as schema version and service identifier.
    /// </remarks>
    public SettingsSectionMetadata SectionMetadata { get; set; }

    /// <summary>
    ///     Saves the current settings to persistent storage if modifications exist.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the save operation.</param>
    /// <returns>
    ///     A task that represents the asynchronous save operation. The task completes when the
    ///     save has finished or the operation has been canceled via <paramref name="cancellationToken"/>.
    /// </returns>
    /// <exception cref="System.InvalidOperationException">
    ///     Thrown if the service is not in a state that allows saving (for example, when required
    ///     dependencies are missing or initialization failed).
    /// </exception>
    public Task SaveAsync(CancellationToken cancellationToken = default);

    /// <summary>
    ///     Validates the current settings and returns any validation errors.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the validation.</param>
    /// <returns>
    ///     A task that represents the asynchronous validation operation. The task result contains a
    ///     read-only list of <see cref="SettingsValidationError"/> describing any problems found.
    ///     An empty list indicates the settings are valid.
    /// </returns>
    public Task<IReadOnlyList<SettingsValidationError>> ValidateAsync(CancellationToken cancellationToken = default);

    /// <summary>
    ///     Resets the settings to their default values.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the reset operation.</param>
    /// <returns>
    ///     A task that represents the asynchronous reset operation. After completion the in-memory
    ///     settings should reflect default values and <see cref="IsDirty"/> may be updated accordingly.
    /// </returns>
    public Task ResetToDefaultsAsync(CancellationToken cancellationToken = default);

    /// <summary>
    ///     Applies properties from cached data (POCO or <c>JsonElement</c>) to this service's settings.
    ///     The method is used by the <c>SettingsManager</c> to update the service when storage sources change.
    /// </summary>
    /// <param name="data">
    ///     The cached data to apply. Can be <c>null</c> to indicate that defaults should be used or the
    ///     section was removed from storage.
    /// </param>
    /// <remarks>
    ///     Implementations should copy matching properties from the provided POCO or JSON to the
    ///     in-memory settings instance. Do not assume the input type; use runtime checks and be robust
    ///     to missing or extra properties. If the data cannot be applied, implementations should
    ///     either leave current values unchanged or reset to defaults — do not throw for simple schema
    ///     differences.
    /// </remarks>
    public void ApplyProperties(object? data);
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// The primary contract for strongly-typed settings service used at runtime.
/// This is the single canonical service contract consumers should use.
/// The service implementation must implement the TSettings interface for direct property access.
/// </summary>
/// <typeparam name="TSettings">The strongly-typed settings interface.</typeparam>
public interface ISettingsService<TSettings> : ISettingsService
    where TSettings : class
{
    /// <summary>
    /// Gets the settings instance that implements the TSettings interface.
    /// This provides typed access to all settings properties.
    /// </summary>
    /// <remarks>
    /// The Settings property provides direct access to settings properties:
    /// <code>
    /// ISettingsService&lt;IEditorSettings&gt; service = ...;
    /// var fontSize = service.Settings.FontSize;
    /// service.Settings.FontSize = 14;
    /// </code>
    /// </remarks>
    public TSettings Settings { get; }

    /// <summary>
    /// Gets a value indicating whether the settings have been modified since the last save.
    /// </summary>
    public bool IsDirty { get; }

    /// <summary>
    /// Gets a value indicating whether the service is currently performing an async operation.
    /// </summary>
    public bool IsBusy { get; }

    /// <summary>
    /// Saves the current settings to persistent storage if modifications exist.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the save operation.</param>
    /// <returns>A task that represents the save operation.</returns>
    public Task SaveAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Forces a reload of settings from all sources, discarding any unsaved changes.
    /// </summary>
    /// <param name="cancellationToken">Token to cancel the reload operation.</param>
    /// <returns>A task that represents the reload operation.</returns>
    public Task ReloadAsync(CancellationToken cancellationToken = default);

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
}

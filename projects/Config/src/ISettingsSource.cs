// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Abstraction for settings sources that handle persistence and serialization/deserialization.
/// Sources are responsible for I/O operations, serialization, and providing structured settings data.
/// </summary>
public interface ISettingsSource
{
    /// <summary>
    /// Event raised when a settings source lifecycle change occurs (added, updated, removed, failed).
    /// </summary>
    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    /// <summary>
    /// Gets the unique identifier for this settings source.
    /// </summary>
    public string Id { get; }

    /// <summary>
    /// Gets a value indicating whether this source supports encryption for sensitive data.
    /// </summary>
    public bool SupportsEncryption { get; }

    /// <summary>
    /// Gets a value indicating whether this source is currently available for operations.
    /// </summary>
    public bool IsAvailable { get; }

    /// <summary>
    /// Gets or sets a value indicating whether this source is currently available for operations.
    /// </summary>
    public bool WatchForChanges { get; set; }

    /// <summary>
    /// Reads settings content from the source.
    /// </summary>
    /// <param name="reload">If true, forces a reload from the underlying source; otherwise, may use cached data.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>A task that represents the read operation. The task result contains the read result.</returns>
    public Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default);

    /// <summary>
    /// Writes settings content to the source using atomic operations.
    /// </summary>
    /// <param name="sectionsData">Dictionary of section names to their serialized content.</param>
    /// <param name="metadata">Metadata to be written alongside the settings.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>A task that represents the write operation. The task result contains the write result.</returns>
    public Task<Result<SettingsWritePayload>> SaveAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        SettingsMetadata metadata,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Validates the settings content without persisting it.
    /// </summary>
    /// <param name="sectionsData">Dictionary of section names to their serialized content.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>A task that represents the validation operation. The task result contains the validation result.</returns>
    public Task<Result<SettingsValidationPayload>> ValidateAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken = default);
}

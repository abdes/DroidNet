// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Abstraction for a settings source that handles persistence, serialization and deserialization of
///     configuration sections.
/// </summary>
public interface ISettingsSource
{
    /// <summary>
    ///     Raised when the source's state or contents change. Typical uses are notifying the manager or
    ///     consumers that the source was added, updated, removed, or failed during an I/O operation.
    /// </summary>
    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    /// <summary>
    ///     Gets the unique identifier for this settings source.
    /// </summary>
    public string Id { get; }

    /// <summary>
    ///     Gets a value indicating whether this source supports encryption for persisted data. When
    ///     <see langword="true"/>, the source will encrypt/decrypt payloads using the configured
    ///     encryption provider.
    /// </summary>
    public bool SupportsEncryption { get; }

    /// <summary>
    ///     Gets a value indicating whether the source is currently available for I/O operations. For
    ///     example, a file-backed source may return <see langword="false"/> if the backing file is
    ///     missing or access is denied.
    /// </summary>
    public bool IsAvailable { get; }

    /// <summary>
    ///     Gets or sets a value indicating whether the source should watch its backing store for
    ///     external changes and raise <see cref="SourceChanged"/> when updates are detected.
    /// </summary>
    public bool WatchForChanges { get; set; }

    /// <summary>
    ///     Gets or sets the source-level metadata attached to this source.
    /// </summary>
    /// <remarks>
    ///     The metadata (for example version, last write time and writer id) is managed by the
    ///     <see cref="SettingsManager"/> and updated when the manager loads or saves settings through
    ///     this source. Implementations should not rely on the metadata for correctness of persistence.
    /// </remarks>
    public SettingsSourceMetadata? SourceMetadata { get; set; }

    /// <summary>
    ///     Reads settings content from the source.
    /// </summary>
    /// <param name="reload">If <see langword="true"/>, forces a read from the underlying store instead of using any cached payload.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>
    ///     A <see cref="Task{TResult}"/> that yields a <see cref="Result{SettingsReadPayload}"/>
    ///     containing the read payload on success or diagnostic errors on failure.
    /// </returns>
    public Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Persists the provided sections to the underlying store. Implementations should perform the
    ///     write atomically when possible so a partially-written state is not visible to readers.
    /// </summary>
    /// <param name="sectionsData">Mapping of section name to the serialized section payload (for example JSON or other blob).</param>
    /// <param name="sectionMetadata">Mapping of section name to its <see cref="SettingsSectionMetadata"/>.</param>
    /// <param name="sourceMetadata">Source-level metadata to associate with the write.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>
    ///     A <see cref="Task{TResult}"/> that yields a <see cref="Result{SettingsWritePayload}"/> describing
    ///     the outcome. On success the payload contains any resulting metadata produced by the source.
    /// </returns>
    public Task<Result<SettingsWritePayload>> SaveAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
        SettingsSourceMetadata sourceMetadata,
        CancellationToken cancellationToken = default);

    /// <summary>
    ///     Validates the provided serialized sections without persisting them. Validation allows a caller
    ///     to check for schema, format or provider-specific constraints before attempting a save.
    /// </summary>
    /// <param name="sectionsData">Mapping of section name to serialized section payload to validate.</param>
    /// <param name="cancellationToken">Token to cancel the operation.</param>
    /// <returns>
    ///     A <see cref="Task{TResult}"/> that yields a <see cref="Result{SettingsValidationPayload}"/>
    ///     describing validation success or returning a set of validation errors.
    /// </returns>
    public Task<Result<SettingsValidationPayload>> ValidateAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken = default);
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Config.Sources;

/// <summary>
///     Base class for settings storage implementations.
///     <para>
///     Implementations of <see cref="ISettingsSource"/> should derive from this type and provide the concrete
///     behavior for loading, saving and validating settings data. The constructor parameters configure the
///     source identifier, optional encryption provider and optional logger factory.
///     </para>
/// </summary>
/// <param name="id">
///     A unique identifier for this settings source. Recommended format: <c>Domain:FileName</c> where the
///     <c>Domain</c> portion can be used to distinguish global, user or built-in application settings.
/// </param>
/// <param name="crypto">
///     An optional <see cref="IEncryptionProvider"/> used to encrypt and decrypt persisted contents. If
///     <see langword="null"/>, <see cref="NoEncryptionProvider.Instance"/> is used and data is stored unencrypted.
/// </param>
/// <param name="loggerFactory">
///     An optional <see cref="ILoggerFactory"/> used to create an <see cref="ILogger{SettingsSource}"/>. If
///     <see langword="null"/>, a <see cref="NullLogger{SettingsSource}"/> is used so logging calls are no-ops.
/// </param>
public abstract class SettingsSource(string id, IEncryptionProvider? crypto = null, ILoggerFactory? loggerFactory = null) : ISettingsSource
{
    private readonly ILogger<SettingsSource> logger = loggerFactory?.CreateLogger<SettingsSource>() ?? NullLogger<SettingsSource>.Instance;

    /// <inheritdoc/>
    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    /// <inheritdoc/>
    public string Id { get; } = id;

    /// <inheritdoc/>
    public bool SupportsEncryption => crypto != null;

    /// <inheritdoc/>
    public abstract bool IsAvailable { get; }

    /// <inheritdoc/>
    public abstract bool WatchForChanges { get; set; }

    /// <inheritdoc/>
    public SettingsSourceMetadata? SourceMetadata { get; set; }

    /// <summary>
    ///     Gets the logger instance for this settings source.
    /// </summary>
    protected ILogger Logger => this.logger;

    /// <inheritdoc/>
    public abstract Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default);

    /// <inheritdoc/>
    public abstract Task<Result<SettingsWritePayload>> SaveAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
        SettingsSourceMetadata sourceMetadata,
        CancellationToken cancellationToken = default);

    /// <inheritdoc/>
    public abstract Task<Result<SettingsValidationPayload>> ValidateAsync(IReadOnlyDictionary<string, object> sectionsData, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Raises the <see cref="SourceChanged"/> event for derived types.
    /// </summary>
    /// <param name="args">The event arguments.</param>
    protected void OnSourceChanged(SourceChangedEventArgs args) => this.SourceChanged?.Invoke(this, args);

    /// <inheritdoc cref="IEncryptionProvider.Encrypt"/>
    protected byte[] Encrypt(byte[] data) => crypto?.Encrypt(data) ?? NoEncryptionProvider.Instance.Encrypt(data);

    /// <inheritdoc cref="IEncryptionProvider.Decrypt"/>
    protected byte[] Decrypt(byte[] data) => crypto?.Decrypt(data) ?? NoEncryptionProvider.Instance.Decrypt(data);
}

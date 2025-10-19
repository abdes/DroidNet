// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace DroidNet.Config;

/// <summary>
/// Metadata associated with settings content.
/// </summary>
public sealed class SettingsMetadata
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsMetadata"/> class.
    /// </summary>
    public SettingsMetadata()
    {
    }

    /// <summary>
    /// Gets the settings content version for migration purposes.
    /// </summary>
    [JsonPropertyName("Version")]
    public string Version { get; init; } = string.Empty;

    /// <summary>
    /// Gets the settings schema version (typically date-based).
    /// </summary>
    [JsonPropertyName("SchemaVersion")]
    public string SchemaVersion { get; init; } = string.Empty;

    /// <summary>
    /// Gets the identifier for the SettingsService that manages this section of settings source (Fully Qualified Type), if available.
    /// </summary>
    [JsonPropertyName("Service")]
    public string? Service { get; init; }

    /// <summary>
    /// Gets the timestamp when the settings were written.
    /// </summary>
    [JsonPropertyName("WrittenAt")]
    public DateTimeOffset WrittenAt { get; init; } = DateTimeOffset.UtcNow;

    /// <summary>
    /// Gets the tool/component that wrote the settings, if available.
    /// </summary>
    [JsonPropertyName("WrittenBy")]
    public string? WrittenBy { get; init; }

    /// <summary>
    /// Creates a new instance with updated timestamp and optional writer information.
    /// </summary>
    /// <param name="writtenBy">Optional tool/component that wrote the settings.</param>
    /// <returns>A new SettingsMetadata instance with updated timestamp.</returns>
    public SettingsMetadata WithUpdate(string? writtenBy = null)
    {
        return new SettingsMetadata
        {
            Version = this.Version,
            SchemaVersion = this.SchemaVersion,
            Service = this.Service,
            WrittenAt = DateTimeOffset.UtcNow,
            WrittenBy = writtenBy ?? this.WrittenBy,
        };
    }

    /// <summary>
    /// Creates a new instance with updated version information.
    /// </summary>
    /// <param name="newVersion">The new version to set.</param>
    /// <param name="writtenBy">Optional tool/component that wrote the settings.</param>
    /// <returns>A new SettingsMetadata instance with updated version and timestamp.</returns>
    public SettingsMetadata WithVersion(string newVersion, string? writtenBy = null)
    {
        return new SettingsMetadata
        {
            Version = newVersion,
            SchemaVersion = this.SchemaVersion,
            Service = this.Service,
            WrittenAt = DateTimeOffset.UtcNow,
            WrittenBy = writtenBy ?? this.WrittenBy,
        };
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace DroidNet.Config;

/// <summary>
///     Metadata associated with settings content.
/// </summary>
public sealed class SettingsMetadata
{
    /// <summary>
    ///     Gets the version of the settings schema.
    /// </summary>
    /// <remarks>
    ///     This value reflects the structure and validation rules for the settings. It only changes when the schema
    ///     itself evolves—such as when new fields are added, types are changed, or validation rules are updated.
    ///     Typically, this is a date or semantic version string.
    /// </remarks>
    [JsonPropertyName("SchemaVersion")]
    public required string SchemaVersion { get; init; } = string.Empty;

    /// <summary>
    ///     Gets the version of the settings content.
    /// </summary>
    /// <remarks>
    ///     This value changes every time the actual configuration data is updated—whether by a user or a system process. Use it
    ///     to track the logical state of the settings and to help with change detection, rollbacks, or conflict resolution.
    /// </remarks>
    [JsonPropertyName("Version")]
    public required string Version { get; init; } = string.Empty;

    /// <summary>
    /// Gets the identifier for the SettingsService that manages this settings section.
    /// </summary>
    /// <remarks>
    ///     This is usually the fully qualified type name of the service or component responsible for the settings. It
    ///     helps clarify ownership and can be useful for diagnostics or tooling.
    /// </remarks>
    [JsonPropertyName("Service")]
    public string? Service { get; init; }

    /// <summary>
    ///     Gets the timestamp when the settings were last written.
    /// </summary>
    /// <remarks>
    ///     This is set automatically whenever the settings are saved or updated. It’s useful for auditing, debugging,
    ///     and understanding the timing of changes.
    /// </remarks>
    [JsonPropertyName("WrittenAt")]
    public DateTimeOffset WrittenAt { get; init; } = DateTimeOffset.UtcNow;

    /// <summary>
    ///     Gets the name of the tool or component that wrote the settings, if available.
    /// </summary>
    /// <remarks>
    ///     This field identifies which application, service, or tool last modified the settings. It’s especially useful in
    ///     environments where multiple tools or services may update configuration data.
    /// </remarks>
    [JsonPropertyName("WrittenBy")]
    public string? WrittenBy { get; init; }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace DroidNet.Config;

/// <summary>
///     Metadata associated with an individual settings section.
/// </summary>
/// <remarks>
///     This metadata is managed by each <see cref="ISettingsService"/> implementation and tracks
///     section-specific information such as schema version and the service that manages this section.
///     Unlike source-level metadata, section metadata is specific to each settings section and can
///     vary independently across different sections within the same source.
/// </remarks>
public sealed class SettingsSectionMetadata
{
    /// <summary>
    ///     Gets the version of the settings schema for this section.
    /// </summary>
    /// <remarks>
    ///     This value reflects the structure and validation rules for the settings section. It only changes
    ///     when the schema itself evolves—such as when new fields are added, types are changed, or validation
    ///     rules are updated. Typically, this is a date in 'yyyyMMdd' format or a semantic version string.
    ///     <para>
    ///     Each <see cref="ISettingsService"/> implementation is responsible for maintaining its own schema version.
    ///     </para>
    /// </remarks>
    [JsonPropertyName("schemaVersion")]
    public required string SchemaVersion { get; init; } = string.Empty;

    /// <summary>
    ///     Gets the identifier for the <see cref="ISettingsService"/> that manages this settings section.
    /// </summary>
    /// <remarks>
    ///     This is usually the fully qualified type name of the service or component responsible for the settings.
    ///     It helps clarify ownership and can be useful for diagnostics, tooling, and debugging.
    ///     <para>
    ///     Example: "MyApp.Settings.EditorSettingsService".
    ///     </para>
    /// </remarks>
    [JsonPropertyName("service")]
    public string? Service { get; init; }
}

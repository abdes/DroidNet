// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace DroidNet.Config;

/// <summary>
///     Metadata associated with the settings source as a whole.
/// </summary>
/// <remarks>
///     This metadata is managed exclusively by the <see cref="SettingsManager"/> and tracks source-level
///     information such as version number, last write time, and the application that wrote the settings.
///     The version is a monotonically increasing counter that increments each time the source is saved
///     with meaningful changes.
/// </remarks>
public sealed class SettingsSourceMetadata
{
    private long version;

    /// <summary>
    ///     Gets the version number of the settings source.
    /// </summary>
    /// <remarks>
    ///     This is a monotonically increasing counter that increments each time the source is saved
    ///     with meaningful changes (i.e., when one or more services are dirty). The version starts at 1
    ///     for newly created sources and increments with each save operation.
    ///     <para>
    ///     This value is managed exclusively by the <see cref="SettingsManager"/> through the
    ///     <see cref="SetVersion"/> method, which requires a <see cref="SetterKey"/> instance.
    ///     </para>
    /// </remarks>
    [JsonPropertyName("version")]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Roslynator", "RCS1085:Use auto-implemented property", Justification = "backing field is important for the SeterKey pattern")]
    public long Version
    {
        get => this.version;
        init => this.version = value;
    }

    /// <summary>
    ///     Gets the timestamp when the settings were last written to this source.
    /// </summary>
    /// <remarks>
    ///     This is set automatically by the <see cref="SettingsManager"/> whenever the source is saved.
    ///     It's useful for auditing, debugging, and understanding the timing of changes.
    /// </remarks>
    [JsonPropertyName("writtenAt")]
    public DateTimeOffset WrittenAt { get; init; } = DateTimeOffset.UtcNow;

    /// <summary>
    ///     Gets the name of the application or tool that wrote the settings, if available.
    /// </summary>
    /// <remarks>
    ///     This field identifies which application, service, or tool last modified the settings source.
    ///     It's especially useful in environments where multiple tools or services may update
    ///     configuration data.
    /// </remarks>
    [JsonPropertyName("writtenBy")]
    public string? WrittenBy { get; init; }

    /// <summary>
    ///     Sets the version number for this metadata.
    /// </summary>
    /// <param name="value">The new version number.</param>
    /// <param name="key">
    ///     The setter key required to modify the version. Only the <see cref="SettingsManager"/>
    ///     can provide a valid key instance.
    /// </param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="key"/> is <see langword="null"/>.</exception>
    /// <remarks>
    ///     This method enforces the setter key pattern, ensuring that only authorized code
    ///     (specifically the <see cref="SettingsManager"/>) can modify the version number.
    /// </remarks>
    internal void SetVersion(long value, SetterKey key)
    {
        ArgumentNullException.ThrowIfNull(key);
        this.version = value;
    }

    /// <summary>
    ///     A key type that restricts write access to the <see cref="Version"/> property.
    /// </summary>
    /// <remarks>
    ///     Only the <see cref="SettingsManager"/> can instantiate this key, ensuring that the version
    ///     can only be modified by the manager itself. This implements the setter key pattern to enforce
    ///     version control at compile time.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "SetterKey is intentionally nested to indicate its close relationship with SettingsSourceMetadata.")]
    public sealed class SetterKey
    {
        /// <summary>
        ///     Initializes a new instance of the <see cref="SetterKey"/> class.
        /// </summary>
        /// <remarks>
        ///     This constructor is internal to ensure only classes within the same assembly
        ///     (specifically <see cref="SettingsManager"/>) can create instances.
        /// </remarks>
        internal SetterKey()
        {
        }
    }
}

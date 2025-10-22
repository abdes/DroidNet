// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Represents the data returned when reading settings from a source.
/// </summary>
public sealed record SettingsReadPayload
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsReadPayload"/> class.
    /// </summary>
    /// <param name="sections">The settings sections that were read from the source. Keys are section names; values are section objects.</param>
    /// <param name="sectionMetadata">
    ///     The metadata for each section, keyed by section name. Each value provides details about the corresponding section.
    /// </param>
    /// <param name="sourceMetadata">The source-level metadata, or <see langword="null"/> if not available.</param>
    /// <param name="sourcePath">The absolute path of the underlying source. Must not be <see langword="null"/> or whitespace.</param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="sections"/> or <paramref name="sectionMetadata"/> is <see langword="null"/>.
    /// </exception>
    /// <exception cref="ArgumentException">Thrown when <paramref name="sourcePath"/> is <see langword="null"/> or whitespace.</exception>
    public SettingsReadPayload(
        IReadOnlyDictionary<string, object> sections,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
        SettingsSourceMetadata? sourceMetadata,
        string sourcePath)
    {
        ArgumentNullException.ThrowIfNull(sections);
        ArgumentNullException.ThrowIfNull(sectionMetadata);

        if (string.IsNullOrWhiteSpace(sourcePath))
        {
            throw new ArgumentException("Source path cannot be null or whitespace.", nameof(sourcePath));
        }

        this.Sections = sections;
        this.SectionMetadata = sectionMetadata;
        this.SourceMetadata = sourceMetadata;
        this.SourcePath = sourcePath;
    }

    /// <summary>
    ///     Gets the sections that were read from the source.
    /// </summary>
    public IReadOnlyDictionary<string, object> Sections { get; }

    /// <summary>
    ///     Gets the metadata for each section, keyed by section name.
    /// </summary>
    public IReadOnlyDictionary<string, SettingsSectionMetadata> SectionMetadata { get; }

    /// <summary>
    ///     Gets the source-level metadata, if any.
    /// </summary>
    /// <remarks>
    ///     This will be <see langword="null"/> if the source has never been written to or if it does not
    ///     contain source metadata (for example, legacy format or newly created source).
    /// </remarks>
    public SettingsSourceMetadata? SourceMetadata { get; }

    /// <summary>
    ///     Gets the absolute path of the underlying source.
    /// </summary>
    public string SourcePath { get; }

    /// <summary>
    ///     Gets the number of sections that were read.
    /// </summary>
    public int SectionCount => this.Sections.Count;
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;

namespace DroidNet.Config;

/// <summary>
/// Represents the data returned when reading settings from a source.
/// </summary>
public sealed record SettingsReadPayload
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsReadPayload"/> class.
    /// </summary>
    /// <param name="sections">The settings sections that were read.</param>
    /// <param name="sectionMetadata">The metadata for each section, keyed by section name.</param>
    /// <param name="sourceMetadata">The source-level metadata, if any.</param>
    /// <param name="sourcePath">The absolute path of the underlying source.</param>
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
    /// Gets the sections that were read from the source.
    /// </summary>
    public IReadOnlyDictionary<string, object> Sections { get; }

    /// <summary>
    /// Gets the metadata for each section, keyed by section name.
    /// </summary>
    public IReadOnlyDictionary<string, SettingsSectionMetadata> SectionMetadata { get; }

    /// <summary>
    /// Gets the source-level metadata, if any.
    /// </summary>
    /// <remarks>
    /// This will be <see langword="null"/> if the source has never been written to or if it doesn't
    /// contain source metadata (e.g., legacy format or newly created source).
    /// </remarks>
    public SettingsSourceMetadata? SourceMetadata { get; }

    /// <summary>
    /// Gets the absolute path of the underlying source.
    /// </summary>
    public string SourcePath { get; }

    /// <summary>
    /// Gets the number of sections that were read.
    /// </summary>
    public int SectionCount => this.Sections.Count;
}

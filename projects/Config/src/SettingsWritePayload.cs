// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Config;

/// <summary>
/// Represents the outcome of a successful write operation.
/// </summary>
public sealed record SettingsWritePayload
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsWritePayload"/> class.
    /// </summary>
    /// <param name="sourceMetadata">The source-level metadata persisted alongside the settings.</param>
    /// <param name="sectionsWritten">The number of sections written during the operation.</param>
    /// <param name="sourcePath">The absolute path of the underlying source.</param>
    public SettingsWritePayload(SettingsSourceMetadata sourceMetadata, int sectionsWritten, string sourcePath)
    {
        ArgumentNullException.ThrowIfNull(sourceMetadata);

        if (string.IsNullOrWhiteSpace(sourcePath))
        {
            throw new ArgumentException("Source path cannot be null or whitespace.", nameof(sourcePath));
        }

        this.SourceMetadata = sourceMetadata;
        this.SectionsWritten = sectionsWritten;
        this.SourcePath = sourcePath;
    }

    /// <summary>
    /// Gets the source-level metadata persisted alongside the settings.
    /// </summary>
    public SettingsSourceMetadata SourceMetadata { get; }

    /// <summary>
    /// Gets the number of sections written during the operation.
    /// </summary>
    public int SectionsWritten { get; }

    /// <summary>
    /// Gets the absolute path of the underlying source.
    /// </summary>
    public string SourcePath { get; }
}

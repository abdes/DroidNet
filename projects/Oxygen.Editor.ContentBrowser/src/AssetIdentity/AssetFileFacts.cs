// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>The size and change time of the file that holds an asset, read from disk without loading it.</summary>
/// <param name="Path">The authored descriptor, source or cooked file, or null when the asset has none.</param>
/// <param name="Size">The file size in bytes, or null when unknown.</param>
/// <param name="Modified">When the file last changed, or null when unknown.</param>
public readonly record struct AssetFileFacts(string? Path, long? Size, DateTimeOffset? Modified)
{
    /// <summary>Gets the size for display, empty when unknown.</summary>
    public string SizeText => this.Size is { } size ? FormatSize(size) : string.Empty;

    /// <summary>Gets the change time for display, empty when unknown.</summary>
    public string ModifiedText => this.Modified is { } modified
        ? modified.ToLocalTime().ToString("g", CultureInfo.CurrentCulture) : string.Empty;

    /// <summary>Returns the file an asset is authored in: its descriptor, else its source, else its cooked file.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The absolute file path, or null when the asset has no file.</returns>
    public static string? GetPath(ContentBrowserAssetItem asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return new[] { asset.DescriptorPath, asset.SourcePath, asset.CookedPath }
            .FirstOrDefault(static path => !string.IsNullOrWhiteSpace(path) && System.IO.Path.IsPathFullyQualified(path));
    }

    /// <summary>Reads the current facts for an asset's file.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The facts; size and time are null when the file is missing or unreadable.</returns>
    public static AssetFileFacts Read(ContentBrowserAssetItem asset)
    {
        var path = GetPath(asset);
        if (path is null)
        {
            return default;
        }

        try
        {
            var file = new FileInfo(path);
            return file.Exists ? new(path, file.Length, file.LastWriteTimeUtc) : new(path, null, null);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return new(path, null, null);
        }
    }

    /// <summary>Formats a byte count the way File Explorer does, in binary units.</summary>
    /// <param name="bytes">The byte count.</param>
    /// <returns>The display size.</returns>
    public static string FormatSize(long bytes)
    {
        string[] units = ["bytes", "KB", "MB", "GB", "TB"];
        double value = bytes;
        var unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }

        return unit == 0
            ? string.Create(CultureInfo.CurrentCulture, $"{bytes:N0} {units[0]}")
            : string.Create(CultureInfo.CurrentCulture, $"{Math.Ceiling(value * 10) / 10:0.#} {units[unit]}");
    }
}

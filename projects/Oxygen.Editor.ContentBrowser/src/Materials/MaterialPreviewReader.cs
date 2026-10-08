// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Security.Cryptography;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>Reads a material's base colour from its saved descriptor for swatch previews.</summary>
internal static class MaterialPreviewReader
{
    private static readonly ConcurrentDictionary<string, (DateTime Written, MaterialPreviewColor? Color)> Cache = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Returns the base colour of a material row, re-reading its descriptor only after it changes.</summary>
    /// <param name="asset">The browser row.</param>
    /// <returns>The base colour, or null for other kinds and for missing or unreadable descriptors.</returns>
    public static MaterialPreviewColor? Read(ContentBrowserAssetItem asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        if (asset.Kind != AssetKind.Material || asset.PrimaryState is AssetState.Missing or AssetState.Broken
            || asset.DescriptorPath is not { } path)
        {
            return null;
        }

        DateTime written;
        try
        {
            written = File.GetLastWriteTimeUtc(path);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return null;
        }

        if (Cache.TryGetValue(path, out var cached) && cached.Written == written)
        {
            return cached.Color;
        }

        var color = TryRead(path);
        Cache[path] = (written, color);
        return color;
    }

    /// <summary>Reads the base colour from a descriptor, optionally only when it still has the expected content hash.</summary>
    /// <param name="descriptorPath">The material descriptor path.</param>
    /// <param name="expectedHash">The saved-source hash the descriptor must match, or null to accept any content.</param>
    /// <returns>The base colour, or null when the descriptor is missing, changed or unreadable.</returns>
    public static MaterialPreviewColor? TryRead(string? descriptorPath, string? expectedHash = null)
    {
        if (descriptorPath is null || !File.Exists(descriptorPath))
        {
            return null;
        }

        try
        {
            var bytes = File.ReadAllBytes(descriptorPath);
            if (expectedHash is not null && !string.Equals(Convert.ToHexString(SHA256.HashData(bytes)), expectedHash, StringComparison.Ordinal))
            {
                return null;
            }

            var source = MaterialSourceReader.Read(bytes);
            var pbr = source.PbrMetallicRoughness;
            return new MaterialPreviewColor(pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or FormatException or System.Text.Json.JsonException)
        {
            return null;
        }
    }
}

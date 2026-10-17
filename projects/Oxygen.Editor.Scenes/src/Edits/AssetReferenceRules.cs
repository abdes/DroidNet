// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits;

/// <summary>The rules every asset reference an edit writes must follow.</summary>
internal static class AssetReferenceRules
{
    /// <summary>Checks that each reference is an absolute asset URI to a file of the expected type.</summary>
    /// <param name="references">The references.</param>
    /// <param name="expectedSuffix">The file suffix of the expected asset type.</param>
    /// <returns>Why a reference is wrong, or <see langword="null"/> when all are right.</returns>
    public static string? Check(IEnumerable<Uri?> references, string expectedSuffix)
    {
        foreach (var uri in references)
        {
            if (uri is null || !uri.IsAbsoluteUri || !string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
            {
                return $"Scene references must use absolute '{AssetUris.Scheme}' asset URIs.";
            }

            // Engine-generated assets, such as the built-in shapes and the default material, have no file.
            if (IsGenerated(uri))
            {
                continue;
            }

            try
            {
                _ = ContentPipelinePaths.ToNativeDescriptorPath(uri, expectedSuffix);
            }
            catch (ArgumentException error)
            {
                return error.Message;
            }
        }

        return null;
    }

    /// <summary>Checks that a reference is an absolute asset URI, whatever the asset's type.</summary>
    /// <param name="reference">The reference.</param>
    /// <returns>Whether it is an absolute asset URI.</returns>
    public static bool IsAssetUri(Uri? reference)
        => reference is { IsAbsoluteUri: true } && string.Equals(reference.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase);

    private static bool IsGenerated(Uri reference)
        => Uri.UnescapeDataString(reference.AbsolutePath).StartsWith($"/{AssetUris.EngineMountPoint}/{AssetUris.GeneratedPath}/", StringComparison.OrdinalIgnoreCase);
}

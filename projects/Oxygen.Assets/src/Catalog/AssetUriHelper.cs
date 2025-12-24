// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Helper class for handling Asset URIs.
/// </summary>
public static class AssetUriHelper
{
    /// <summary>
    /// The URI scheme for assets.
    /// </summary>
    public const string Scheme = "asset";

    /// <summary>
    /// Creates an asset URI from a mount point and a relative path.
    /// </summary>
    /// <param name="mountPoint">The mount point name.</param>
    /// <param name="relativePath">The path relative to the mount point.</param>
    /// <returns>A valid asset URI.</returns>
    public static Uri CreateUri(string mountPoint, string relativePath)
    {
        ArgumentException.ThrowIfNullOrEmpty(mountPoint);

        // Normalize relative path
        var normalizedPath = relativePath.Replace('\\', '/').TrimStart('/');

        // Construct path: /MountPoint/RelativePath
        // We use the path component to store the mount point to support characters that are invalid in a URI Authority (e.g. spaces).
        var path = $"{mountPoint}/{normalizedPath}";

        // Ensure leading slash to prevent UriBuilder from creating opaque URIs (e.g. asset:path)
        if (!path.StartsWith('/'))
        {
            path = "/" + path;
        }

        var builder = new UriBuilder(Scheme, "")
        {
            Path = path
        };
        return builder.Uri;
    }

    /// <summary>
    /// Gets the mount point name from an asset URI.
    /// </summary>
    /// <param name="uri">The asset URI.</param>
    /// <returns>The mount point name, or empty string if not found.</returns>
    public static string GetMountPoint(Uri uri)
    {
        if (!string.Equals(uri.Scheme, Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return string.Empty;
        }

        // If Authority is present, use it (backward compatibility)
        if (!string.IsNullOrEmpty(uri.Authority))
        {
            return Uri.UnescapeDataString(uri.Authority);
        }

        // Otherwise, extract from path
        // AbsolutePath: /MountPoint/RelativePath
        var path = uri.AbsolutePath.TrimStart('/');
        var firstSlash = path.IndexOf('/');
        if (firstSlash < 0)
        {
            return Uri.UnescapeDataString(path);
        }

        return Uri.UnescapeDataString(path.Substring(0, firstSlash));
    }

    /// <summary>
    /// Gets the path relative to the mount point from an asset URI.
    /// </summary>
    /// <param name="uri">The asset URI.</param>
    /// <returns>The relative path.</returns>
    public static string GetRelativePath(Uri uri)
    {
        if (!string.Equals(uri.Scheme, Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return string.Empty;
        }

        if (!string.IsNullOrEmpty(uri.Authority))
        {
            return uri.AbsolutePath.TrimStart('/');
        }

        var path = uri.AbsolutePath.TrimStart('/');
        var firstSlash = path.IndexOf('/');
        if (firstSlash < 0)
        {
            return string.Empty;
        }

        return Uri.UnescapeDataString(path.Substring(firstSlash + 1));
    }

    /// <summary>
    /// Gets the full virtual path (e.g. /MountPoint/Path/To/File) from an asset URI.
    /// </summary>
    /// <param name="uri">The asset URI.</param>
    /// <returns>The full virtual path.</returns>
    public static string GetVirtualPath(Uri uri)
    {
        if (!string.Equals(uri.Scheme, Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return string.Empty;
        }

        if (!string.IsNullOrEmpty(uri.Authority))
        {
            return "/" + Uri.UnescapeDataString(uri.Authority) + Uri.UnescapeDataString(uri.AbsolutePath);
        }

        return Uri.UnescapeDataString(uri.AbsolutePath);
    }
}

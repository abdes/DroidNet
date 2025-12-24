// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog;

internal static class AssetQueryScopeMatcher
{
    public static bool IsMatch(AssetQueryScope scope, Uri assetUri)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(assetUri);

        if (scope.Traversal == AssetQueryTraversal.All)
        {
            return true;
        }

        if (scope.Roots.Count == 0)
        {
            return false;
        }

        foreach (var root in scope.Roots)
        {
            if (root is null)
            {
                continue;
            }

            if (IsMatchRoot(scope.Traversal, root, assetUri))
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsMatchRoot(AssetQueryTraversal traversal, Uri root, Uri asset)
        => traversal switch
        {
            AssetQueryTraversal.Self => IsSameAsset(root, asset),
            AssetQueryTraversal.Descendants => IsDescendant(root, asset),
            AssetQueryTraversal.Children => IsImmediateChild(root, asset),
            _ => false,
        };

    private static bool IsSameAsset(Uri root, Uri asset)
        => string.Equals(root.Scheme, asset.Scheme, StringComparison.OrdinalIgnoreCase)
            && string.Equals(AssetUriHelper.GetMountPoint(root), AssetUriHelper.GetMountPoint(asset), StringComparison.OrdinalIgnoreCase)
            && string.Equals(AssetUriHelper.GetRelativePath(root), AssetUriHelper.GetRelativePath(asset), StringComparison.Ordinal);

    private static bool IsDescendant(Uri root, Uri asset)
    {
        if (!IsSameAuthority(root, asset))
        {
            return false;
        }

        var rootPath = EnsureFolderPath(AssetUriHelper.GetRelativePath(root));
        var assetPath = AssetUriHelper.GetRelativePath(asset);

        if (!assetPath.StartsWith(rootPath, StringComparison.Ordinal))
        {
            return false;
        }

        // Root must be a strict prefix for descendants.
        return assetPath.Length > rootPath.Length;
    }

    private static bool IsImmediateChild(Uri root, Uri asset)
    {
        if (!IsSameAuthority(root, asset))
        {
            return false;
        }

        var rootPath = EnsureFolderPath(AssetUriHelper.GetRelativePath(root));
        var assetPath = AssetUriHelper.GetRelativePath(asset);

        if (!assetPath.StartsWith(rootPath, StringComparison.Ordinal))
        {
            return false;
        }

        if (assetPath.Length <= rootPath.Length)
        {
            return false;
        }

        // Immediate child means: exactly one segment after the root folder.
        var remainder = assetPath[rootPath.Length..];
        remainder = remainder.Trim('/');
        return remainder.Length > 0 && remainder.IndexOf('/', StringComparison.Ordinal) < 0;
    }

    private static bool IsSameAuthority(Uri root, Uri asset)
        => string.Equals(root.Scheme, asset.Scheme, StringComparison.OrdinalIgnoreCase)
            && string.Equals(AssetUriHelper.GetMountPoint(root), AssetUriHelper.GetMountPoint(asset), StringComparison.OrdinalIgnoreCase);

    private static string EnsureFolderPath(string path)
        => string.IsNullOrEmpty(path)
            ? string.Empty
            : path.EndsWith('/')
                ? path
                : path + "/";
}

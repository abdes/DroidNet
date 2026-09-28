// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Services;

internal static class MaterialOverridePathMapper
{
    public static string? ToEnginePath(Uri? materialUri)
    {
        if (materialUri is null)
        {
            return null;
        }

        if (!materialUri.IsAbsoluteUri || materialUri.Scheme != AssetUriHelper.Scheme
            || materialUri.Query.Length != 0 || materialUri.Fragment.Length != 0)
        {
            throw new ArgumentException("A material requires an absolute asset URI without query or fragment.", nameof(materialUri));
        }

        if (string.Equals(AssetUriHelper.GetMountPoint(materialUri), "__uninitialized__", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("An uninitialized material is not a clear operation; use null.", nameof(materialUri));
        }

        var virtualPath = AssetUriHelper.GetVirtualPath(materialUri);
        return virtualPath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
            ? virtualPath[..^".json".Length]
            : virtualPath;
    }
}

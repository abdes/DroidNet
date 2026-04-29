// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;

namespace Oxygen.Editor.World.Services;

internal static class MaterialOverridePathMapper
{
    public static string? ToEnginePath(Uri? materialUri)
    {
        if (materialUri is null)
        {
            return null;
        }

        if (string.Equals(AssetUriHelper.GetMountPoint(materialUri), "__uninitialized__", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        var virtualPath = AssetUriHelper.GetVirtualPath(materialUri);
        return virtualPath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
            ? virtualPath[..^".json".Length]
            : virtualPath;
    }
}

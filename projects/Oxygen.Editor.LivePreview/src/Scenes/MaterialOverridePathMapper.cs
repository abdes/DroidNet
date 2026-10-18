// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.LivePreview.Scenes;

internal static class MaterialOverridePathMapper
{
    public static string? ToEnginePath(Uri? materialUri)
    {
        if (materialUri is null)
        {
            return null;
        }

        if (!materialUri.IsAbsoluteUri
            || !string.Equals(materialUri.Scheme, AssetUriHelper.Scheme, StringComparison.Ordinal)
            || materialUri.Query.Length != 0 || materialUri.Fragment.Length != 0)
        {
            throw new ArgumentException("A material requires an absolute asset URI without query or fragment.", nameof(materialUri));
        }

        var virtualPath = AssetUriHelper.GetVirtualPath(materialUri);
        return virtualPath.EndsWith(AssetFileTypes.MaterialDescriptor, StringComparison.OrdinalIgnoreCase)
            ? virtualPath[..^".json".Length]
            : virtualPath;
    }
}

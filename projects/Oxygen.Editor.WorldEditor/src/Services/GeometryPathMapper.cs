// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Services;

/// <summary>Preserves authored geometry identity while addressing its runtime descriptor.</summary>
internal static class GeometryPathMapper
{
    /// <summary>Maps a geometry source or built-in URI to its runtime reference.</summary>
    /// <param name="uri">The authored geometry identity.</param>
    /// <returns>The corresponding engine path.</returns>
    public static string ToEnginePath(Uri uri)
    {
        var path = AssetUriHelper.GetEnginePath(uri);
        return path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase) ? path[..^5] : path;
    }
}

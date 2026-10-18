// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Preserves authored geometry identity while addressing its runtime descriptor.</summary>
internal static class GeometryPathMapper
{
    /// <summary>Maps a geometry asset or built-in to its runtime reference.</summary>
    /// <param name="geometry">The authored geometry identity.</param>
    /// <returns>The corresponding engine path.</returns>
    public static string ToEnginePath(AssetPath geometry)
    {
        ArgumentNullException.ThrowIfNull(geometry);
        return geometry.VirtualPath;
    }
}

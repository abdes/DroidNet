// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>
/// Defines the scope of an asset catalog query.
/// </summary>
/// <remarks>
/// <para>
/// A root is a mount (<c>/Content</c>, <c>/Engine</c>), a folder (<c>/Content/Textures</c>) or, with
/// <see cref="AssetQueryTraversal.Self"/>, an asset. The traversal selects the root itself, its children or all its
/// descendants, comparing whole segments.
/// </para>
/// </remarks>
public sealed record AssetQueryScope(IReadOnlyList<AssetPath> Roots, AssetQueryTraversal Traversal)
{
    /// <summary>
    /// Gets a scope that targets all assets across all providers/mount points.
    /// </summary>
    public static AssetQueryScope All { get; } = new([], AssetQueryTraversal.All);
}

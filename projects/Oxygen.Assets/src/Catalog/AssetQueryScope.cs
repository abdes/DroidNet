// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Defines the scope of an asset catalog query.
/// </summary>
/// <remarks>
/// <para>
/// Scope is expressed in terms of canonical asset URIs. A root is typically either:
/// <list type="bullet">
/// <item><description>A mount point root: <c>asset://Content/</c>, <c>asset://Engine/</c></description></item>
/// <item><description>A folder root: <c>asset://Content/Textures/</c></description></item>
/// </list>
/// </para>
/// <para>
/// The catalog decides how to interpret roots (prefix match on authority + path segments is the
/// expected default). Consumers should treat the semantics as "best effort" across providers.
/// </para>
/// </remarks>
public sealed record AssetQueryScope(IReadOnlyList<Uri> Roots, AssetQueryTraversal Traversal)
{
    /// <summary>
    /// Gets a scope that targets all assets across all providers/mount points.
    /// </summary>
    public static AssetQueryScope All { get; } = new([], AssetQueryTraversal.All);
}

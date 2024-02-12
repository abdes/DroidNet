// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Utils;

using System.Diagnostics;
using DroidNet.Routing.Contracts;

/// <summary>
/// Utility class for resolving a <see cref="UrlTree" /> relative to a
/// <see cref="ActiveRoute" />.
/// </summary>
internal static class RelativeUrlTreeResolver
{
    /// <summary>
    /// Resolves a <see cref="UrlTree" /> relative to the given
    /// <see cref="IActiveRoute" />.
    /// </summary>
    /// <param name="urlTree">The url tree to resolve.</param>
    /// <param name="relativeTo">
    /// The route relative to which the resolution will happen.
    /// </param>
    /// <remarks>
    /// This method implements a simple relative resolution within the
    /// constraints of valid routing URLs. Only the segments of the primary
    /// top-level <see cref="UrlSegmentGroup" /> are implicated.
    /// </remarks>
    /// <exception cref="InvalidOperationException">
    /// if the url tree is absolute or id the resolution goes deeper than what
    /// the <paramref name="relativeTo" /> route permits.
    /// </exception>
    /// <returns>
    /// A new absolute <see cref="UrlTree" />, but still referencing the
    /// original branches from <paramref name="urlTree" />.
    /// </returns>
    public static UrlTree ResolveUrlTreeRelativeTo(UrlTree urlTree, IActiveRoute relativeTo)
    {
        if (!urlTree.IsRelative)
        {
            throw new InvalidOperationException("attempting relative resolution of an absolute url");
        }

        _ = urlTree.Root.Children.TryGetValue(OutletName.Primary, out var primary);
        Debug.Assert(primary is not null, "if the url tree is relative, it must have a child for the primary outlet");

        var numLevelsUp = 0;
        foreach (var segment in primary.Segments)
        {
            if (segment.Path.Equals("..", StringComparison.Ordinal))
            {
                numLevelsUp++;
            }
            else
            {
                break;
            }
        }

        var resolvedSegments = new List<UrlSegment>();

        var origin = relativeTo;
        while (origin.Parent != null)
        {
            resolvedSegments.InsertRange(0, origin.UrlSegments);
            origin = origin.Parent;
        }

        if (resolvedSegments.Count < numLevelsUp)
        {
            throw new InvalidOperationException("relative tree goes deeper then the root of the active route");
        }

        resolvedSegments.RemoveRange(resolvedSegments.Count - numLevelsUp, numLevelsUp);
        resolvedSegments.AddRange(primary.Segments.GetRange(numLevelsUp, primary.Segments.Count - numLevelsUp));

        var root = new UrlSegmentGroup([]);
        root.AddChild(
            OutletName.Primary,
            new UrlSegmentGroup(resolvedSegments, primary.Children.ToDictionary()));
        return new UrlTree(root, urlTree.QueryParams);
    }
}

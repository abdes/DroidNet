// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Routing.Utils;

/// <summary>
/// Resolves relative URL trees within the routing system, transforming them into absolute URL trees
/// based on the current navigation context.
/// </summary>
/// <remarks>
/// The <see cref="RelativeUrlTreeResolver"/> class is responsible for interpreting and resolving
/// relative URLs within the context of the current navigation state. It ensures that relative paths
/// are correctly transformed into absolute paths, taking into account the current route hierarchy
/// and any active parameters.
/// </remarks>
internal static class RelativeUrlTreeResolver
{
    /// <summary>
    /// Resolves a <see cref="UrlTree" /> relative to the given <see cref="IActiveRoute" />.
    /// </summary>
    /// <param name="urlTree">The URL tree to resolve.</param>
    /// <param name="relativeTo">The route relative to which the resolution will happen.</param>
    /// <returns>
    /// A new absolute <see cref="UrlTree" />, but still referencing the original branches from
    /// <paramref name="urlTree" />.
    /// </returns>
    /// <exception cref="InvalidOperationException">
    /// Thrown when the URL tree is absolute or if the resolution goes deeper than what the
    /// <paramref name="relativeTo" /> route permits.
    /// </exception>
    /// <remarks>
    /// This method implements a simple relative resolution within the constraints of valid routing
    /// URLs. Only the segments of the primary top-level <see cref="UrlSegmentGroup" /> are
    /// implicated. It ensures that relative paths like ".." are correctly interpreted to navigate
    /// up the route hierarchy.
    /// <para>
    /// For example, if the current route is "/users/123" and the relative URL tree is
    /// "../settings", this method will resolve the absolute URL tree to "/settings".
    /// </para>
    /// </remarks>
    public static UrlTree ResolveUrlTreeRelativeTo(IUrlTree urlTree, IActiveRoute relativeTo)
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

        var resolvedSegments = new List<IUrlSegment>();

        var origin = relativeTo;
        while (origin.Parent != null)
        {
            resolvedSegments.InsertRange(0, origin.Segments);
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
        return new UrlTree(root, urlTree.QueryParams) { IsRelative = false };
    }
}

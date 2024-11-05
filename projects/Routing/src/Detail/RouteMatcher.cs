// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

using System.Collections.ObjectModel;
using System.Diagnostics;
using DroidNet.Routing.Utils;

internal static class RouteMatcher
{
    /// <summary>
    /// Default path matcher used by the router to match a route's <see cref="Path" /> against segments in the url. Override this default
    /// matcher by setting the route's <see cref="RouteMatcher" /> property.
    /// </summary>
    /// <inheritdoc cref="IRoute.PathMatcher" />
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "MA0051:Method is too long",
        Justification = "commenting the code makes the method too long but is useful")]
    internal static IMatchResult MatchRoute(
        ReadOnlyCollection<IUrlSegment> segments,
        IUrlSegmentGroup group,
        IRoute route)
    {
        Debug.Assert(
            route.Path is not null,
            "when the default matcher is being used, a route must have a non-null path");

        // Empty path route with Prefix match method matches anything but does
        // not consume the segment unless it is the empty path.
        if (route.Path.Length == 0 && route.MatchMethod == PathMatch.StrictPrefix)
        {
            return new Route.Match()
            {
                Consumed = segments[0].Path.Length == 0 ? segments.GetRange(0, 1) : [],
            };
        }

        var parts = route.Path.Split('/');

        if (parts.Length > segments.Count)
        {
            return new Route.NoMatch();
        }

        if (route.MatchMethod == PathMatch.Full &&
            (group.Children is not { Count: 0 } || parts.Length < segments.Count))
        {
            // The route's Path is longer than the actual segments, or there
            // are more segments in the children, but we are looking for a full
            // match.
            return new Route.NoMatch();
        }

        if (route.MatchMethod == PathMatch.StrictPrefix && parts.Length < segments.Count)
        {
            // The route's Path is longer than the actual segments, but we are
            // looking for a full match.
            return new Route.NoMatch();
        }

        Dictionary<string, IUrlSegment> posParams = [];

        // Check each config part against the actual URL
        for (var index = 0; index < parts.Length; index++)
        {
            var part = parts[index];
            var segment = segments[index];
            var isParameter = part.StartsWith(':');
            if (isParameter)
            {
                // Parameters match any segment
                posParams[part[1..]] = segment;
            }
            else if (!string.Equals(part, segment.Path, StringComparison.Ordinal))
            {
                return new Route.NoMatch()
                {
                    Consumed = segments.GetRange(0, index),
                };
            }
        }

        return new Route.Match()
        {
            Consumed = segments.GetRange(0, parts.Length),
            PositionalParams = posParams,
        };
    }
}

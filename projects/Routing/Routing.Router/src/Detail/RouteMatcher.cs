// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Routing.Utils;

namespace DroidNet.Routing.Detail;

/// <summary>
/// Provides methods for matching URL segments against route configurations within the routing system.
/// </summary>
/// <remarks>
/// The <see cref="RouteMatcher"/> class contains the default path matching logic used by the router to determine if a route's path matches the segments in a URL.
/// This class supports both full and prefix matching strategies and handles routes with both empty and non-empty paths.
/// </remarks>
internal static class RouteMatcher
{
    /// <summary>
    /// Matches a route's path against the provided URL segments using the default path matching logic.
    /// </summary>
    /// <param name="segments">The list of URL segments to match against the route's path.</param>
    /// <param name="group">The segment group containing the segments.</param>
    /// <param name="route">The route to match against the segments.</param>
    /// <returns>
    /// An <see cref="IMatchResult"/> indicating whether the match was successful and including any extracted parameters.
    /// </returns>
    /// <remarks>
    /// This method attempts to match the provided URL segments against the route's path. It handles
    /// both empty and non-empty paths, and supports both full and prefix matching strategies.
    /// <para>
    /// For routes with empty paths, the method determines whether the route can match based on the
    /// route's <see cref="Route.MatchMethod"/> and the presence of remaining segments or child
    /// segment groups. An empty-path route can match if either there are no remaining segments and
    /// no child segments, or if the route allows prefix matching.
    /// </para>
    /// <para>
    /// For routes with non-empty paths, the method splits the route's path into parts and attempts
    /// to match each part against the corresponding URL segment. If the route's match method is set
    /// to <see cref="PathMatch.Full"/>, the method ensures that all segments are consumed and that
    /// there are no remaining child segments.
    /// </para>
    /// <para>
    /// This method handles parameterized paths by extracting parameter values from the segments.
    /// </para>
    /// </remarks>
    internal static IMatchResult MatchRoute(
        IReadOnlyList<IUrlSegment> segments,
        IUrlSegmentGroup group,
        IRoute route)
    {
        Debug.Assert(
            route.Path is not null,
            "when the default matcher is being used, a route must have a non-null path");

        return TryMatchEmptyPath(segments, group, route) ?? MatchNonEmptyPath(segments, group, route);
    }

    /// <summary>
    /// Matches a route with a non-empty path against the provided URL segments.
    /// </summary>
    /// <param name="segments">The list of URL segments to match against the route's path.</param>
    /// <param name="group">The segment group containing the segments.</param>
    /// <param name="route">The route to match against the segments.</param>
    /// <returns>
    /// An <see cref="IMatchResult"/> indicating whether the match was successful and including any extracted parameters.
    /// </returns>
    /// <remarks>
    /// This method splits the route's path into parts and attempts to match each part against the
    /// corresponding URL segment. It handles parameterized paths by extracting parameter values
    /// from the segments. If the route's match method is set to <see cref="PathMatch.Full"/>, the
    /// method ensures that all segments are consumed and that there are no remaining child
    /// segments.
    /// </remarks>
    private static IMatchResult MatchNonEmptyPath(
       IReadOnlyList<IUrlSegment> segments,
       IUrlSegmentGroup group,
       IRoute route)
    {
        var parts = route.Path!.Split('/');

        if (parts.Length > segments.Count)
        {
            return new Route.NoMatch();
        }

        if (route.MatchMethod == PathMatch.Full &&
            (group.Children is not { Count: 0 } || parts.Length < segments.Count))
        {
            // The route's Path is longer than the actual segments, or there are more segments in the children, but we
            // are looking for a full match.
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

    /// <summary>
    /// Attempts to match a route with an empty path against the provided URL segments.
    /// </summary>
    /// <param name="segments">The list of URL segments to match against the route's path.</param>
    /// <param name="group">The segment group containing the segments.</param>
    /// <param name="route">The route to match against the segments.</param>
    /// <returns>
    /// An <see cref="IMatchResult"/> indicating whether the match was successful, or <see langword="null"/> if the route does not have an empty path.
    /// </returns>
    /// <remarks>
    /// This method handles the special case of routes with empty paths. It determines whether the
    /// route can match based on the route's <see cref="Route.MatchMethod"/> and the presence of
    /// remaining segments or child segment groups. An empty-path route can match if either there
    /// are no remaining segments and no child segments, or if the route allows prefix matching.
    /// </remarks>
    private static IMatchResult? TryMatchEmptyPath(
        IReadOnlyList<IUrlSegment> segments,
        IUrlSegmentGroup group,
        IRoute route) => route.Path!.Length != 0
            ? null
            : route.MatchMethod == PathMatch.Full
            ? group.Children.Count == 0 && segments.Count == 0
                ? new Route.Match() { Consumed = [] }
                : new Route.NoMatch()
            : new Route.Match
            {
                // Empty path route with Prefix match method matches anything but does
                // not consume the segment unless the segment's Path is the empty path.
                Consumed = segments.Count > 0 && segments[0].Path.Length == 0 ? segments.GetRange(0, 1) : [],
            };
}

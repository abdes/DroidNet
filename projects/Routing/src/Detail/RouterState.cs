// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

using System.Collections.ObjectModel;
using System.Diagnostics;
using DroidNet.Routing.Utils;

/// <summary>
/// Represents the current state of the application router.
/// </summary>
/// <param name="url">
/// The navigation url used to create this router state.
/// </param>
/// <param name="urlTree">
/// The parsed <see cref="UrlTree" /> corresponding to the <paramref name="url" />.
/// </param>
/// <param name="root">The root <see cref="IActiveRoute" /> of the state.</param>
internal sealed class RouterState(string url, IUrlTree urlTree, IActiveRoute root) : IRouterState
{
    private interface IExtendedMatchResult : IMatchResult
    {
        IActiveRoute? LastActiveRoute { get; }
    }

    public string Url { get; set; } = url;

    public IActiveRoute RootNode { get; } = root;

    public IUrlTree UrlTree { get; } = urlTree;

    /// <summary>
    /// Creates a <see cref="RouterState" /> from a <see cref="UrlTree" /> by attempting to match each node in the
    /// <paramref name="urlTree">tree</paramref> to the routes in the <paramref name="routes">router config</paramref>.
    /// </summary>
    /// <param name="urlTree">
    /// The <see cref="UrlTree" /> from which to generate the new router state.
    /// </param>
    /// <param name="routes">
    /// The <see cref="Router.Config">routes</see> against which the url tree segment groups will be matched.
    /// </param>
    /// <returns>
    /// A <see cref="RouterState" />, not yet activated, corresponding to the given <paramref name="urlTree" />.
    /// </returns>
    /// <remarks>
    /// When trying to match a URL to a route, the router looks at the unmatched segments of the URL and tries to find a path that
    /// will match, or consume a segment. The router takes a depth-first approach to matching URL segments with paths, having to
    /// backtrack sometimes to continue trying the next routes if the current one fails to match. This means that the first path
    /// of routes to fully consume a URL wins.
    /// <para>
    /// You must take care with how you structure your router configuration, as there is no notion of specificity or importance
    /// amongst routes — the first match always wins. Order matters. Once all segments of the URL have been consumed, we say that
    /// a match has occurred.
    /// </para>
    /// </remarks>
    public static IRouterState CreateFromUrlTree(IUrlTree urlTree, IRoutes routes)
    {
        var stateRoot = new ActiveRoute()
        {
            UrlSegmentGroup = urlTree.Root,
            UrlSegments = Array.Empty<UrlSegment>(),
            Outlet = OutletName.Primary,
            Params = ReadOnlyParameters.Empty,
            QueryParams = urlTree.QueryParams,
            RouteConfig = new Route()
            {
                Path = string.Empty,
                MatchMethod = PathMatch.Full,
                Children = routes,
            },
        };

        Recognize(stateRoot, routes, urlTree);

        return new RouterState(urlTree.ToString() ?? string.Empty, urlTree, stateRoot);
    }

    private static void Recognize(IActiveRoute state, IRoutes routes, IUrlTree urlTree)
        => ProcessSegmentGroup(state, routes, urlTree.Root, OutletName.Primary);

    private static void ProcessSegmentGroup(
        IActiveRoute state,
        IRoutes routes,
        IUrlSegmentGroup segmentGroup,
        OutletName outlet)
    {
        if (segmentGroup.Segments.Count == 0 && segmentGroup.Children.Count != 0)
        {
            ProcessChildren(state, segmentGroup);
            return;
        }

        var matchResult = MatchSegments(state, routes, segmentGroup, segmentGroup.Segments, outlet);
        if (!matchResult.IsMatch)
        {
            throw new NoRouteForSegmentsException
            {
                Segments = segmentGroup.Segments,
            };
        }

        Debug.Assert(
            matchResult.LastActiveRoute != null,
            "expecting non-null last active route when a match is successful");
        ProcessChildren(matchResult.LastActiveRoute, segmentGroup);
    }

    private static void ProcessChildren(
        IActiveRoute state,
        IUrlSegmentGroup segmentGroup)
    {
        var children = segmentGroup.SortedChildren;
        if (children.Count == 0)
        {
            return;
        }

        var routes = state.RouteConfig.Children ??
                     throw new InvalidOperationException("unexpected null route config");

        foreach (var child in children)
        {
            var sortedRoutes = routes.SortedByMatchingOutlet(child.Key);
            ProcessSegmentGroup(state, sortedRoutes, child.Value, child.Key);
        }
    }

    /// <summary>
    /// Determines which routes in the config match the given segments and constructs the active route hierarchy accordingly.
    /// </summary>
    /// <param name="state">The router state.</param>
    /// <param name="routes">The routes' configuration.</param>
    /// <param name="segmentGroup">The segment group to which the segments being matched belong.</param>
    /// <param name="segments">The segments to match to routes.</param>
    /// <param name="outlet">The outlet name of route being parsed.</param>
    /// <remarks>
    /// Any time the URL changes, the router derives a new set of parameters from it: the router takes the positional parameters
    /// (e.g., ‘:id’) of the matched URL segments and the matrix parameters of the last matched URL segment and combines those.
    /// This operation is pure: the URL has to change for the parameters to change. Or in other words, the same URL will always
    /// result in the same set of parameters.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "MA0051:Method is too long",
        Justification = "many parameters in method calls occupy too many lines")]
    private static IExtendedMatchResult MatchSegments(
        IActiveRoute state,
        IRoutes routes,
        IUrlSegmentGroup segmentGroup,
        ReadOnlyCollection<IUrlSegment> segments,
        OutletName outlet)
    {
        IActiveRoute? newState = null;

        foreach (var route in routes)
        {
            var matchResult = route.Matcher(segments, segmentGroup, route);
            if (!matchResult.IsMatch)
            {
                continue;
            }

            IActiveRoute lastActiveRoute = new ActiveRoute()
            {
                Outlet = outlet,
                Params = MergeParameters(segments, matchResult),
                QueryParams = state.QueryParams,
                RouteConfig = route,
                UrlSegments = new List<IUrlSegment>(matchResult.Consumed).AsReadOnly(),
                UrlSegmentGroup = segmentGroup,
            };
            if (newState == null)
            {
                newState = lastActiveRoute;
            }
            else
            {
                newState.AddChild(lastActiveRoute);
            }

            // If the route has children, continue matching with the remaining segments and child routes
            if (route.Children != null && matchResult.Consumed.Count < segments.Count)
            {
                var remainingSegments = segments.GetRange(
                    matchResult.Consumed.Count,
                    segments.Count - matchResult.Consumed.Count);
                var childMatchResult = MatchSegments(
                    newState,
                    route.Children,
                    segmentGroup,
                    remainingSegments.AsReadOnly(),
                    outlet);
                if (childMatchResult.IsMatch)
                {
                    Debug.Assert(
                        childMatchResult.LastActiveRoute is not null,
                        $"expecting {nameof(lastActiveRoute)} to be not null when a match is successful");
                    lastActiveRoute = childMatchResult.LastActiveRoute;

                    // Merge the consumed segments and positional parameters from the parent and child match results
                    AddChildConsumedTokens(matchResult.Consumed, childMatchResult.Consumed);
                    foreach (var param in childMatchResult.PositionalParams)
                    {
                        matchResult.PositionalParams[param.Key] = param.Value;
                    }
                }
                else
                {
                    // If the child routes didn't match, backtrack and try the next route
                    newState = newState.Parent;
                    continue;
                }
            }

            // If we've consumed all the segments, return the match result
            if (matchResult.Consumed.Count == segments.Count)
            {
                state.AddChild(newState);
                return new Match()
                {
                    LastActiveRoute = lastActiveRoute,
                    PositionalParams = matchResult.PositionalParams,
                    Consumed = matchResult.Consumed,
                };
            }
        }

        // If no routes matched, return null
        return new NoMatch();

        static Parameters MergeParameters(ReadOnlyCollection<IUrlSegment> segments, IMatchResult matchResult)
        {
            // Merge the positional parameters with the matrix parameters of the last segment (only the last segment)
            // into the activated route parameters.
            var parameters = new Parameters();
            foreach (var pair in matchResult.PositionalParams)
            {
                parameters.AddOrUpdate(pair.Key, pair.Value.Path);
            }

            if (segments.Count == 0)
            {
                return parameters;
            }

            var lastSegment = segments[^1];
            foreach (var parameter in lastSegment.Parameters)
            {
                parameters.AddOrUpdate(parameter.Name, parameter.Value);
            }

            return parameters;
        }

        static void AddChildConsumedTokens(IList<IUrlSegment> consumed, IEnumerable<IUrlSegment> additionalSegments)
        {
            if (consumed is List<IUrlSegment> parentAsList)
            {
                parentAsList.AddRange(additionalSegments);
                return;
            }

            foreach (var item in additionalSegments)
            {
                consumed.Add(item);
            }
        }
    }

    private sealed class Match : Route.Match, IExtendedMatchResult
    {
        public required IActiveRoute LastActiveRoute { get; init; }
    }

    private sealed class NoMatch : Route.NoMatch, IExtendedMatchResult
    {
        public IActiveRoute? LastActiveRoute => null;
    }
}

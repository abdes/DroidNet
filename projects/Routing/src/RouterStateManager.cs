// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections.ObjectModel;
using System.Diagnostics;
using DroidNet.Routing.Detail;
using DroidNet.Routing.Utils;

// TODO(abdes): state manager with history

/// <summary>
/// Simple implementation for <see cref="IRouterStateManager" /> that can only
/// remember the current navigation.
/// </summary>
/// <param name="routerConfig">
/// The router <see cref="Router.Config" /> against which the url tree segment
/// groups will be matched.
/// </param>
public class RouterStateManager(IRoutes routerConfig) : IRouterStateManager
{
    private interface IExtendedMatchResult : IMatchResult
    {
        IActiveRoute? LastActiveRoute { get; }
    }

    /// <summary>
    /// Generates <see cref="RouterState" /> from a <see cref="UrlTree" /> by
    /// attempting to match each node in the tree to the routes in the router
    /// config.
    /// </summary>
    /// <param name="urlTree">
    /// The <see cref="UrlTree" /> from which to generate the new router state.
    /// </param>
    /// <returns>
    /// A <see cref="RouterState" />, not yet activated, corresponding to the
    /// given <paramref name="urlTree" />.
    /// </returns>
    /// <remarks>
    /// When trying to match a URL to a route, the router looks at the
    /// unmatched segments of the URL and tries to find a path that will match,
    /// or consume a segment. The router takes a depth-first approach to
    /// matching URL segments with paths, having to backtrack sometimes to
    /// continue trying the next routes if the current one fails to match. This
    /// means that the first path of routes to fully consume a URL wins. You
    /// must take care with how you structure your router configuration, as
    /// there is no notion of specificity or importance amongst routes — the
    /// first match always wins. Order matters. Once all segments of the URL
    /// have been consumed, we say that a match has occurred.
    /// </remarks>
    public IRouterState CreateFromUrlTree(IUrlTree urlTree)
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
                Children = routerConfig,
            },
        };

        ProcessSegmentGroup(stateRoot, routerConfig, urlTree.Root, OutletName.Primary);

        return new RouterState(urlTree.ToString() ?? string.Empty, urlTree, stateRoot);
    }

    private static void ProcessSegmentGroup(
        IActiveRoute state,
        IRoutes config,
        IUrlSegmentGroup segmentGroup,
        OutletName outlet)
    {
        if (segmentGroup.Segments.Count == 0 && segmentGroup.Children.Count != 0)
        {
            ProcessChildren(state, segmentGroup);
            return;
        }

        ProcessSegments(state, config, segmentGroup, segmentGroup.Segments, outlet);
    }

    private static void ProcessSegments(
        IActiveRoute state,
        IRoutes config,
        IUrlSegmentGroup segmentGroup,
        ReadOnlyCollection<IUrlSegment> segments,
        OutletName outlet)
    {
        var matchResult = MatchSegments(state, config, segmentGroup, segments, outlet);
        if (!matchResult.IsMatch)
        {
            var root = segmentGroup;
            while (root.Parent != null)
            {
                root = root.Parent;
            }

            throw new NoRouteForSegmentsException(segments, root);
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

        var config = state.RouteConfig.Children ?? throw new InvalidOperationException("unexpected null route config");

        foreach (var child in children)
        {
            var sortedConfig = config.SortedByMatchingOutlet(child.Key);
            ProcessSegmentGroup(state, sortedConfig, child.Value, child.Key);
        }
    }

    /// <summary>
    /// Determines which routes in the config match the given segments and
    /// constructs the active route hierarchy accordingly.
    /// </summary>
    /// <remarks>
    /// Any time the URL changes, the router derives a new set of parameters
    /// from it: the router takes the positional parameters (e.g., ‘:id’) of
    /// the matched URL segments and the matrix parameters of the last matched
    /// URL segment and combines those. This operation is pure: the URL has to
    /// change for the parameters to change. Or in other words, the same URL
    /// will always result in the same set of parameters.
    /// </remarks>
    private static IExtendedMatchResult MatchSegments(
        IActiveRoute state,
        IRoutes config,
        IUrlSegmentGroup segmentGroup,
        ReadOnlyCollection<IUrlSegment> segments,
        OutletName outlet)
    {
        IActiveRoute? newState = null;

        foreach (var route in config)
        {
            var matchResult = route.Matcher(segments, segmentGroup, route);
            if (!matchResult.IsMatch)
            {
                continue;
            }

            // Merge the positional parameters with the matrix parameters of
            // the last segment (only the last segment) into the activated
            // route parameters.
            var parameters = new Parameters();
            foreach (var pair in matchResult.PositionalParams)
            {
                parameters.AddOrUpdate(pair.Key, pair.Value.Path);
            }

            if (segments.Count != 0)
            {
                var lastSegment = segments[^1];
                foreach (var parameter in lastSegment.Parameters)
                {
                    parameters.AddOrUpdate(parameter.Name, parameter.Value);
                }
            }

            IActiveRoute lastActiveRoute = new ActiveRoute()
            {
                Outlet = outlet,
                Params = parameters,
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
                    matchResult.Consumed.AddRange(childMatchResult.Consumed);
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
    }

    private class Match : Route.Match, IExtendedMatchResult
    {
        public required IActiveRoute LastActiveRoute { get; init; }
    }

    private class NoMatch : Route.NoMatch, IExtendedMatchResult
    {
        public IActiveRoute? LastActiveRoute => null;
    }
}

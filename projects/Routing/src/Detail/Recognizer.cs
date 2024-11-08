// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

using System.Diagnostics;
using DroidNet.Routing.Utils;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Parameters = DroidNet.Routing.Parameters;

/// <summary>
/// The recognizer is a key part of the router navigation. It efficiently matches the URL tree to the router
/// configuration, producing a router state that can be the basis for the activation stage.
/// </summary>
/// <param name="urlSerializer">
/// The <see cref="IUrlSerializer" /> to use to serialize a <see cref="IUrlTree" /> being recognized into a URL string.
/// </param>
/// <param name="config"> The router configuration.</param>
/// <param name="loggerFactory">
/// Used to obtain a logger for this class. If not possible, a <see cref="NullLogger" /> will be used instead.
/// </param>
internal sealed partial class Recognizer(
    IUrlSerializer urlSerializer,
    IRoutes config,
    ILoggerFactory? loggerFactory)
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<Recognizer>() ??
                                      NullLoggerFactory.Instance.CreateLogger<Recognizer>();

    private interface IExtendedMatchResult : IMatchResult
    {
        ActiveRoute? ContributedState { get; }
    }

    /// <summary>
    /// Creates a <see cref="RouterState" /> from a <see cref="UrlTree" /> by attempting to match each node in the
    /// <paramref name="urlTree">tree</paramref> to the routes in the router config.
    /// </summary>
    /// <param name="urlTree">
    /// The <see cref="UrlTree" /> from which to generate the new router state.
    /// </param>
    /// <returns>
    /// A <see cref="RouterState" />, not yet activated, corresponding to the given <paramref name="urlTree" />; or null
    /// if matching of the <paramref name="urlTree" /> to the <see cref="config" /> failed.
    /// </returns>
    /// <remarks>
    /// When trying to match a URL to a route, the router looks at the unmatched segments of the URL and tries to find a
    /// path that will match, or consume a segment. The router takes a depth-first approach to matching URL segments
    /// with paths, having to backtrack sometimes to continue trying the next routes if the current one fails to match.
    /// This means that the first path of routes to fully consume a URL wins.
    /// <para>
    /// You must take care with how you structure your router configuration, as there is no notion of specificity or
    /// importance amongst routes — the first match always wins. Order matters. Once all segments of the URL have been
    /// consumed, we say that a match has occurred.
    /// </para>
    /// </remarks>
    public RouterState? Recognize(IUrlTree urlTree)
    {
        this.LogStartRecognition(urlTree, config);

        // We start with an empty router state as the root. This root has no value
        // for the activation, except from holding the URL tree and its query params
        // during the recognition.
        var state = new ActiveRoute
        {
            SegmentGroup = urlTree.Root,
            Segments = [],
            Outlet = OutletName.Primary,
            Params = new Parameters(),
            QueryParams = urlTree.QueryParams,
            Config = new Route()
            {
                Path = string.Empty,
                MatchMethod = PathMatch.Full,
                Children = config,
            },
        };

        try
        {
            this.LogProgress($"Process root segment group o={OutletName.Primary} sg={urlTree.Root}", config);
            var states = this.ProcessSegmentGroup(state, urlTree.Root, OutletName.Primary, config);

            if (states.Count != 1)
            {
                this.LogBadConfiguration(urlTree, config);
                return null;
            }

            this.LogRecognitionSuccessful(urlTree);
            return new RouterState(urlSerializer.Serialize(urlTree), urlTree, states[0]);
        }
        catch (NoRouteForSegmentsException ex)
        {
            this.LogBadNavigation(ex.Message, urlTree, config);

            return null;
        }
    }

    private static bool EmptyPathMatch(IUrlSegmentGroup segmentGroup, IReadOnlyList<IUrlSegment> segments, IRoute route)
    {
        if ((segmentGroup.Children.Count != 0 || segments.Count > 0) && route.MatchMethod == PathMatch.Full)
        {
            return false;
        }

        return route.Path?.Length == 0;
    }

    private static IExtendedMatchResult MatchSegmentsAgainstRouteOnce(
        ActiveRoute parentState,
        IUrlSegmentGroup segmentGroup,
        IReadOnlyList<IUrlSegment> segments,
        IRoute route)
    {
        if (route.Path?.Length == 0)
        {
            return route.MatchMethod == PathMatch.Full && (segments.Count > 0 || segmentGroup.Children.Count != 0)
                ? new NoMatch()
                : new Match
                {
                    // Consume any empty path segment
                    Consumed = segments is [{ Path.Length: 0 }] ? [new UrlSegment(string.Empty)] : [],
                    ContributedState = new ActiveRoute
                    {
                        SegmentGroup = segmentGroup,
                        Segments = [],
                        Outlet = route.Outlet,
                        Params = parentState.Params,
                        QueryParams = parentState.QueryParams,
                        Config = route,
                        ViewModel = route.ViewModelType,
                    },
                };
        }

        var result = route.Matcher(segments, segmentGroup, route);
        if (!result.IsMatch)
        {
            return new NoMatch();
        }

        var newState = new ActiveRoute
        {
            SegmentGroup = segmentGroup,
            Segments = result.Consumed.AsReadOnly(),
            Outlet = route.Outlet,
            Params = Parameters.Merge(
                parentState.Params,
                MergeLocalParameters(result.PositionalParams, result.Consumed[^1].Parameters)),
            QueryParams = parentState.QueryParams,
            Config = route,
            ViewModel = route.ViewModelType,
        };

        return new Match
        {
            Consumed = result.Consumed,
            ContributedState = newState,
        };

        static Parameters MergeLocalParameters(
            IDictionary<string, IUrlSegment> positional,
            IParameters matrix)
        {
            var merged = new Parameters();

            // Merge the positional parameters
            foreach (var pair in positional)
            {
                merged.AddOrUpdate(pair.Key, pair.Value.Path);
            }

            // Merge the matrix parameters of the last segment (only the last segment)
            foreach (var parameter in matrix)
            {
                merged.AddOrUpdate(parameter.Name, parameter.Value);
            }

            return merged;
        }
    }

    private List<ActiveRoute> ProcessSegmentGroup(
        ActiveRoute parentState,
        IUrlSegmentGroup segmentGroup,
        OutletName outlet,
        IRoutes routes)
    {
        var segments = segmentGroup.Segments.ToList();

        // If the routes have empty path matches, and the segments are empty, add
        // an empty path segment to the segment group so we can match the empty
        // path route before we go for the children
        if (segments.Count == 0 && routes.Any(r => EmptyPathMatch(segmentGroup, segments, r)))
        {
            segments.Add(new UrlSegment(string.Empty));
        }

        if (segments.Count == 0 && segmentGroup.Children.Count != 0)
        {
            return this.ProcessChildren(parentState, segmentGroup, routes);
        }

        var segmentsState = this.ProcessSegments(parentState, segmentGroup, segments, outlet, routes);
        return segmentsState is null ? [] : [segmentsState];
    }

    private List<ActiveRoute> ProcessChildren(ActiveRoute parentState, IUrlSegmentGroup segmentGroup, IRoutes routes)
    {
        if (segmentGroup.Children.Count == 0)
        {
            return [];
        }

        this.LogProgress($"Process children of segment group sg={segmentGroup}", routes);

        var childStates = new List<ActiveRoute>();

        // Process child outlets one at a time, starting with the primary outlet.
        foreach (var child in segmentGroup.SortedChildren)
        {
            this.LogProgress($"Process child segment group o={child.Key} sg={segmentGroup}", routes);
            var contributedStates = this.ProcessSegmentGroup(parentState, child.Value, child.Key, routes);
            if (contributedStates.Count == 0)
            {
                this.LogProgress($"No match for child segment group o={child.Key} sg={segmentGroup}", routes);
                throw new NoRouteForSegmentsException
                {
                    Segments = child.Value.Segments,
                };
            }

            if (contributedStates.Count == 1)
            {
                var state = contributedStates[0];
                this.LogProgress($"Match contributed 1 active route, {state}", routes);
                childStates.Add(state);
            }
            else
            {
                this.LogProgress(
                    $"Match contributed {contributedStates.Count} active routes, {contributedStates}",
                    routes);

                // Params and QueryParams are empty until the child state is added to its parent state.
                // At that time, both will be updated and propagated to the child's children.
                var childState = new ActiveRoute
                {
                    SegmentGroup = child.Value,
                    Segments = child.Value.Segments,
                    Outlet = child.Key,
                    Params = parentState.Params,
                    QueryParams = parentState.QueryParams,
                    Config = new Route()
                    {
                        Path = string.Empty,
                        Children = routes,
                    },
                };
                childState.AddChildren(contributedStates);
                childStates.Add(childState);
            }
        }

        return childStates;
    }

    private ActiveRoute? ProcessSegments(
        ActiveRoute parentState,
        IUrlSegmentGroup segmentGroup,
        List<IUrlSegment> segments,
        OutletName outlet,
        IRoutes routes)
    {
        this.LogProgress($"Process segments {string.Join('/', segments)}", routes);

        var match = this.MatchSegments(parentState, segmentGroup, segments, outlet, routes);
        if (!match.IsMatch)
        {
            return null;
        }

        // We have a match, which may have contributed multiple active routes.
        // We need to continue from the last contributed route.
        var lastState = GetLastChild(match.ContributedState!);
        var childrenStates = this.ProcessChildren(lastState, lastState.SegmentGroup, lastState.Config.Children);
        lastState.AddChildren(childrenStates);
        return match.ContributedState;

        static ActiveRoute GetLastChild(ActiveRoute node)
        {
            while (node.Children.Count > 0)
            {
                Debug.Assert(
                    node.Children.Count == 1,
                    "active routes returned from MatchSegments should only have 0 or 1 child");
                node = (ActiveRoute)node.Children.First();
            }

            return node;
        }
    }

    private IExtendedMatchResult MatchSegments(
        ActiveRoute parentState,
        IUrlSegmentGroup segmentGroup,
        IReadOnlyList<IUrlSegment> segments,
        OutletName outlet,
        IRoutes routes)
    {
        foreach (var route in routes.SortedByMatchingOutlet(outlet))
        {
            var match = this.MatchSegmentsAgainstRoute(parentState, segmentGroup, segments, outlet, route);
            if (match.IsMatch)
            {
                return match;
            }
        }

        return new NoMatch();
    }

    private IExtendedMatchResult MatchSegmentsAgainstRoute(
        ActiveRoute parentState,
        IUrlSegmentGroup segmentGroup,
        IReadOnlyList<IUrlSegment> segments,
        OutletName outlet,
        IRoute route)
    {
        // We allow matches to empty paths when the outlets differ so we can match a url like `/(b:b)` to
        // a config like
        // * `{path: '', children: [{path: 'b', outlet: 'b'}]}`
        // or even
        // * `{path: '', outlet: 'a', children: [{path: 'b', outlet: 'b'}]` <- FIXME: this scenario, detect empty path route with named outlet
        if (route.Outlet != outlet && (outlet.IsPrimary || !EmptyPathMatch(segmentGroup, segmentGroup.Segments, route)))
        {
            return new NoMatch();
        }

        var match = MatchSegmentsAgainstRouteOnce(parentState, segmentGroup, segments, route);
        if (!match.IsMatch)
        {
            return new NoMatch();
        }

        // We have a match!

        // If the route has children, and we have not consumed all segments, continue
        // matching with the remaining segments and child routes, otherwise just
        // capture the matched route.
        if (route.Children.Count == 0 || match.Consumed.Count == segments.Count)
        {
            return match;
        }

        var remainingSegments = segments.GetRange(
            match.Consumed.Count,
            segments.Count - match.Consumed.Count);

        var segmentsState = match.ContributedState!;
        var deepMatch = this.MatchSegments(
            segmentsState,
            segmentGroup,
            remainingSegments.AsReadOnly(),
            outlet,
            route.Children);
        if (!deepMatch.IsMatch)
        {
            return new NoMatch();
        }

        segmentsState.AddChild(deepMatch.ContributedState!);
        foreach (var consumedSegment in deepMatch.Consumed)
        {
            match.Consumed.Add(consumedSegment);
        }

        return match;
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Starting recognition for `{UrlTree}` against Config={@Config}")]
    private partial void LogStartRecognition(IUrlTree urlTree, IRoutes config);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Completed successful recognition for `{UrlTree}`")]
    private partial void LogRecognitionSuccessful(IUrlTree urlTree);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Route recognition did not produce a router root state, Tree={@UrlTree} Config={@Config}")]
    private partial void LogBadConfiguration(IUrlTree urlTree, IRoutes config);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message
            = "Failed to fully match the url using the router config, because of `{Because}`, Tree={@UrlTree} Config={@Config}")]
    private partial void LogBadNavigation(string because, IUrlTree urlTree, IRoutes config);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "{Message} c={Config}")]
    private partial void LogProgress(string message, IRoutes config);

    private sealed class Match : Route.Match, IExtendedMatchResult
    {
        public required ActiveRoute ContributedState { get; init; }
    }

    private sealed class NoMatch : Route.NoMatch, IExtendedMatchResult
    {
        public ActiveRoute? ContributedState => null;
    }
}

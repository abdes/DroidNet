// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Routing.Utils;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Routing.Detail;

/// <summary>
/// Matches URL trees against route configurations to build router state during navigation.
/// </summary>
/// <param name="urlSerializer">
/// The URL serializer used to convert between string URLs and URL trees. This component handles
/// the parsing and formatting of URLs according to the routing system's conventions.
/// </param>
/// <param name="config">
/// The route configuration against which URLs will be matched. This defines the complete set of
/// valid routes and their corresponding view models.
/// </param>
/// <param name="loggerFactory">
/// Optional factory for creating loggers. If provided, enables detailed logging of the recognition
/// process. If <see langword="null"/>, logging is disabled.
/// </param>
/// <remarks>
/// <para>
/// The Recognizer is responsible for transforming a URL into a tree of active routes. During
/// navigation, it takes a URL tree and attempts to match each segment against the route
/// configurations, building up a corresponding router state that reflects the matched routes
/// and their parameters.
/// </para>
/// <para>
/// The matching process involves recursively traversing the URL tree's segment groups and
/// attempting to match them with the routes in the configuration. This recursive descent
/// handles both primary segments and auxiliary segments (those in named outlets), building
/// up a comprehensive router state that the router can use to activate views.
/// </para>
/// <para>
/// If recognition fails because segments cannot be matched or the URL structure doesn't align
/// with the route configuration, the recognizer logs detailed diagnostics to help identify the
/// mismatch.
/// </para>
/// </remarks>
/// <example>
/// <strong>Recognizing a URL</strong>
/// <para>
/// Consider this navigation URL:
/// </para>
/// <code><![CDATA[
/// /workspace/(main:editor//side:explorer//bottom:output)
/// ]]></code>
/// <para>
/// When matched against a route configuration like:
/// </para>
/// <code><![CDATA[
/// new Routes([
///     new Route {
///         Path = "workspace",
///         ViewModelType = typeof(WorkspaceViewModel),
///         Children = new Routes([
///             new Route {
///                 Path = "editor",
///                 ViewModelType = typeof(EditorViewModel)
///             },
///             new Route {
///                 Path = "explorer",
///                 Outlet = "side",
///                 ViewModelType = typeof(ExplorerViewModel)
///             },
///             new Route {
///                 Path = "output",
///                 Outlet = "bottom",
///                 ViewModelType = typeof(OutputViewModel)
///             }
///         ])
///     }
/// ])
/// ]]></code>
/// <para>
/// The recognizer creates an active route tree where the WorkspaceViewModel hosts three child
/// routes - EditorViewModel in the primary outlet, ExplorerViewModel in the "side" outlet, and
/// OutputViewModel in the "bottom" outlet. Each active route captures its URL parameters, query
/// parameters, and maintains its position in the hierarchy.
/// </para>
/// </example>
internal sealed partial class Recognizer(
    IUrlSerializer urlSerializer,
    IRoutes config,
    ILoggerFactory? loggerFactory)
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<Recognizer>() ??
                                      NullLoggerFactory.Instance.CreateLogger<Recognizer>();

    /// <summary>
    /// Represents the result of matching URL segments against a route's path configuration,
    /// with additional state contribution information.
    /// </summary>
    private interface IExtendedMatchResult : IMatchResult
    {
        /// <summary>
        /// Gets the state contributed by this match result, if any.
        /// </summary>
        ActiveRoute? ContributedState { get; }
    }

    /// <summary>
    /// Creates a <see cref="RouterState"/> from a <see cref="IUrlTree"/> by attempting to match each node in the
    /// <paramref name="urlTree"/> to the routes in the router configuration.
    /// </summary>
    /// <param name="urlTree">The <see cref="IUrlTree"/> from which to generate the new router state.</param>
    /// <returns>
    /// A <see cref="RouterState"/>, not yet activated, corresponding to the given <paramref name="urlTree"/>;
    /// or <see langword="null"/> if matching of the <paramref name="urlTree"/> to the <see cref="IRoutes"/>
    /// configuration failed.
    /// </returns>
    /// <remarks>
    /// This method serves as the entry point for the recognition process.
    /// When trying to match a URL to a route, the recognizer examines the unmatched segments of the URL
    /// and attempts to find a path that will match or consume a segment. The recognizer employs a depth-first
    /// approach to matching URL segments with paths, sometimes needing to backtrack to try the next routes
    /// if the current one fails to match. This means that the first path of routes to fully consume a URL wins.
    /// <para>
    /// Care must be taken when structuring the router configuration, as there is no notion of specificity or
    /// importance among routes—the first match always wins. Order matters. Once all segments of the URL have
    /// been consumed, a match is considered to have occurred.
    /// </para>
    /// <para>
    /// The recursive nature of this method allows it to handle complex URL structures with nested segments
    /// and auxiliary routes. By building up the router state as it matches each segment group, it constructs
    /// a hierarchical representation of the routes that corresponds to the original URL. If the recognition
    /// process completes successfully, it returns the constructed router state; otherwise, it logs an error
    /// and returns <see langword="null"/>.
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

    /// <summary>
    /// Determines if a route with an empty path matches the given segment group and segments.
    /// </summary>
    /// <param name="segmentGroup">The segment group to match against.</param>
    /// <param name="segments">The list of URL segments to match.</param>
    /// <param name="route">The route to match.</param>
    /// <returns>
    /// <see langword="true"/> if the route with an empty path matches the segment group and segments; otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// If the route has an empty path, the method determines whether it can match based on the route's
    /// <see cref="PathMatch"/> method and the presence of remaining segments or child segment groups. An empty-path
    /// route can match if either there are no remaining segments and no child segments, or if the route allows
    /// prefix matching.
    /// </remarks>
    private static bool EmptyPathMatch(IUrlSegmentGroup segmentGroup, IReadOnlyList<IUrlSegment> segments, IRoute route)
    => ((segmentGroup.Children.Count == 0 && segments.Count == 0) || route.MatchMethod != PathMatch.Full) && route.Path?.Length == 0;

    /// <summary>
    /// Attempts to match the provided URL segments against a specific route exactly once, without recursion.
    /// </summary>
    /// <param name="parentState">The current active route representing the parent in the router state tree.</param>
    /// <param name="segmentGroup">The segment group containing the segments to match.</param>
    /// <param name="segments">The list of URL segments to match against the route's path.</param>
    /// <param name="route">The route to attempt to match against.</param>
    /// <returns>
    /// An <see cref="IExtendedMatchResult"/> indicating whether the match was successful and any contributed state.
    /// </returns>
    /// <remarks>
    /// This method contributes to the recognition process by matching a single route against the
    /// current set of segments, providing the result to higher-level methods that handle recursive
    /// traversal and processing of child routes. It is a critical step that determines whether a
    /// route corresponds to a portion of the URL, forming the basis for building the active route
    /// tree. It attempts the match exactly <strong>once</strong>, without considering child routes
    /// or additional segment groups.
    /// <para>
    /// If the route has an empty path, the method determines whether it can match based on the
    /// route's <see cref="PathMatch"/> method and the presence of remaining segments or child
    /// segment groups. An empty-path route can match if either there are no remaining segments and
    /// no child segments, or if the route allows prefix matching.
    /// </para>
    /// <para>
    /// For routes with non-empty paths, the method uses the route's matcher to attempt to match the
    /// segments. If the match is successful, it creates an <see cref="ActiveRoute"/> representing
    /// the matched route, including any parameters extracted during matching. Those parameters are
    /// fully available for the segment group own usage, but only the matrix parameters of <strong>
    /// the last segment in the group</strong> will be propagated to the children.
    /// </para>
    /// </remarks>
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
            Segments = result.Consumed,
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
            Consumed = new List<IUrlSegment>(result.Consumed),
            ContributedState = newState,
        };

        static Parameters MergeLocalParameters(
            IReadOnlyDictionary<string, IUrlSegment> positional,
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

    /// <summary>
    /// Recursively processes a segment group and attempts to match it against the route configuration.
    /// </summary>
    /// <param name="parentState">The active route representing the parent route in the router state tree.</param>
    /// <param name="segmentGroup">The segment group to process, containing the URL segments and any child segment groups (auxiliary routes).</param>
    /// <param name="outlet">The name of the outlet corresponding to this segment group (primary or named outlet). From a recursive decent point of view, this outlet name is the same than the key under which this segment group is stored as a child of its parent segment group. </param>
    /// <param name="routes">The collection of routes to match against the segments in the segment group.</param>
    /// <returns>
    /// <see langword="true"/> if the segment group was successfully matched and processed; otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// <para>
    /// This method initiates the recognition process, ensuring that if the routes have empty path
    /// matches and the segments are empty, the method still needs to match the empty path route.To
    /// achieve this, a segment with an empty path is implicitly added to the segment group,
    /// allowing the empty path route to be matched before processing the children.
    /// </para>
    /// <para>
    /// After calling <see cref="ProcessSegments"/> for the primary segments, the method recursively
    /// processes any child segment groups(auxiliary routes) by iterating over the children of the
    /// segment group and calling <see cref="ProcessChildren"/> for each one.
    /// </para>
    /// </remarks>
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

    /// <summary>
    /// Attempts to match the provided URL segments against the available routes and updates the router state accordingly.
    /// </summary>
    /// <param name="parentState">The active route representing the parent route in the router state tree.</param>
    /// <param name="segmentGroup">The segment group containing the segments to be matched.</param>
    /// <param name="segments">The list of URL segments to match against the routes.</param>
    /// <param name="outlet">The name of the outlet corresponding to this segment group (primary or named outlet).</param>
    /// <param name="routes">The collection of routes to attempt to match against the segments.</param>
    /// <returns>
    /// An <see cref="ActiveRoute"/> instance representing the matched route and its state, or <see langword="null"/>
    /// if no match is found.
    /// </returns>
    /// <remarks>
    /// <para>
    /// This method is a core part of the recognition process, handling the bulk of recursive traversal of
    /// the URL tree's segment groups. It attempts to match the provided segment group against the
    /// available routes and builds up the router state accordingly.
    /// </para>
    /// <para>
    /// For a recognition to succeed, all segments in the segment group must be successfully
    /// matched, and their children if any must also be successfully match.
    /// </para>
    /// <para>
    /// A successful match of the primary segments, may contribute several active routes to the
    /// router state (nested routes and multi-segment groups). The last active route in the
    /// contributed state from the match is used to continue the recursive descent with the segment
    /// group children (auxiliary routes).
    /// </para>
    /// <para>
    /// The method contributes to the overall router state construction by building an active route
    /// tree that mirrors the structure of the URL, handling both primary and auxiliary routes,
    /// recursively processing nested segments and routes, and managing outlet names to ensure that
    /// content is loaded into the correct outlets. This recursive processing enables the recognizer
    /// to handle complex URL structures with nested paths and multiple outlets, ultimately
    /// constructing a complete router state that reflects the entire navigation tree.
    /// </para>
    /// </remarks>
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

    /// <summary>
    /// Attempts to find the first route, in a set of routes, that matches the provided URL segments and returns the matching result.
    /// </summary>
    /// <param name="parentState">The active route representing the parent in the router state tree.</param>
    /// <param name="segmentGroup">The segment group containing the segments to match.</param>
    /// <param name="segments">The list of URL segments to match against the routes.</param>
    /// <param name="outlet">The outlet name for the segment group (primary or named outlet).</param>
    /// <param name="routes">The collection of routes to attempt to match against the segments.</param>
    /// <returns>
    /// An <see cref="IExtendedMatchResult"/> indicating whether a match was found and including any contributed state.
    /// </returns>
    /// <remarks>
    /// This method iterates over the provided routes, sorted in a way that puts routes with the
    /// same outlet name than <paramref name="outlet"/>, and attempts to match the given segments
    /// against each one of them. The first route to match will be stop the iteration and return a
    /// successful match. If none of the routes match, a no-match result is returned.
    /// </remarks>
    /// <seealso cref="IRoutes.SortedByMatchingOutlet"/>
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

    /// <summary>
    /// Recursively processes the child segment groups of a given segment group and attempts to match them against the child routes.
    /// </summary>
    /// <param name="parentState">The active route representing the parent route in the router state tree.</param>
    /// <param name="segmentGroup">The segment group containing the child segment groups (auxiliary routes) to process.</param>
    /// <param name="routes">The collection of child routes to match against the child segment groups.</param>
    /// <returns>
    /// <see langword="true"/> if all child segment groups were successfully matched and processed; otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// This method is integral to the recognition process, as it handles the matching of child
    /// segment groups to their corresponding child routes. It recursively traverses each child
    /// segment group, whether they represent primary child routes or auxiliary routes within named
    /// outlets, by invoking <see cref="ProcessSegmentGroup"/> for each one. By matching each child
    /// segment group to its appropriate route configuration, the method builds the hierarchical
    /// structure of the active route tree.
    /// <para>
    /// The method ensures that each child segment group is matched correctly, contributing to the
    /// comprehensive and accurate construction of the router state. If any child segment group
    /// fails to match its corresponding routes, the method returns <see langword="false"/>,
    /// indicating that the overall recognition process cannot proceed successfully.
    /// </para>
    /// </remarks>
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

    /// <summary>
    /// Attempts to match a list of URL segments against a specific route.
    /// </summary>
    /// <param name="parentState">The current active route representing the parent in the router state tree.</param>
    /// <param name="segmentGroup">The segment group containing the segments to match.</param>
    /// <param name="segments">The list of segments to match.</param>
    /// <param name="outlet">The outlet name for the segment group.</param>
    /// <param name="route">The route to attempt to match against.</param>
    /// <returns>
    /// An <see cref="IExtendedMatchResult"/> indicating whether the match was successful and including any contributed state.
    /// </returns>
    /// <remarks>
    /// This method focuses on matching the provided segments against a single route. It uses the
    /// route's matcher to determine if the segments correspond to the route's path. If a match is
    /// found, it creates a new active route representing the matched route, including any
    /// parameters extracted from the segments.
    /// <para>
    /// Because the matching algorithm favors the deepest match in the routes config for as many
    /// segments as possible in a segment group, the will recursivel attempt to match the remaining
    /// segments to child routes by calling <see cref="MatchSegments"/> again until no more segments
    /// are left; otherwise it's a no-match. This allows it to handle nested routes and build up the
    /// router state for the matched route and its descendants.
    /// </para>
    /// </remarks>
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
        remainingSegments,
        outlet,
        route.Children);
        if (!deepMatch.IsMatch)
        {
            return new NoMatch();
        }

        segmentsState.AddChild(deepMatch.ContributedState!);
        var allConsumedSegments = new List<IUrlSegment>(match.Consumed);
        allConsumedSegments.AddRange(deepMatch.Consumed);
        return new Match
        {
            Consumed = allConsumedSegments,
            ContributedState = match.ContributedState!,
        };
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

    /// <summary>
    /// Represents a successful match.
    /// </summary>
    /// <remarks>
    /// Instances of this class will always have their <see cref="IMatchResult.IsMatch"/> property
    /// be <see langword="true"/>, and will always have a non-empty collection of
    /// <see cref="IMatchResult.Consumed"/> segments.
    /// </remarks>
    private sealed class Match : Route.Match, IExtendedMatchResult
    {
        /// <summary>
        /// Gets the state contributed by this match result, if any.
        /// </summary>
        public required ActiveRoute ContributedState { get; init; }
    }

    /// <summary>
    /// Represents a failed match.
    /// </summary>
    /// <remarks>
    /// Instances of this class may have segments in the <see cref="IMatchResult.Consumed"/>
    /// collection, representing route segments that have been successfully matched up to until the
    /// overall match was deemed a failure.
    /// </remarks>
    private sealed class NoMatch : Route.NoMatch, IExtendedMatchResult
    {
        /// <inheritdoc/>
        public ActiveRoute? ContributedState => null;
    }
}

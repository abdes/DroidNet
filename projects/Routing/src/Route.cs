// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System;
using System.Collections.Immutable;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Text;
using DroidNet.Routing.Utils;

/// <summary>
/// Enum representing different types of matching for routes.
/// </summary>
public enum PathMatch
{
    /// <summary>
    /// Default matching strategy when the <see cref="Route.MatchMethod" /> property
    /// was not explicitly set. Equivalent to <see cref="Prefix" />.
    /// </summary>
    Default,

    /// <summary>
    /// Matches when the path starts with the route path.
    /// </summary>
    Prefix,

    /// <summary>
    /// Matches when the path starts with the route path, but fails if the
    /// current segment group does not exactly match the route.
    /// </summary>
    /// <remarks>
    /// This is particularly useful for wide matching routes such as <c>""</c>,
    /// which may also require that no segments in the current segment group
    /// should remain after the match.
    /// </remarks>
    StrictPrefix,

    /// <summary>
    /// Matches when the path is exactly equal to the route path.
    /// </summary>
    Full,
}

/// <summary>
/// Represents a single route definition including its corresponding view model
/// type, and child routes.
/// </summary>
public class Route
{
    /// <summary>
    /// Represents a function for matching a route's <see cref="Path">path</see>
    /// against <see cref="UrlSegment">segments</see> in
    /// a <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <param name="segments">
    /// The list of segments, against which the route's <see cref="Path">path</see>
    /// is to be matched. This could be a subset of the segments in the
    /// <paramref name="group" /> (typically, the remaining unmatched segments
    /// so far).
    /// </param>
    /// <param name="group">
    /// The <see cref="UrlSegmentGroup" /> to which the <paramref name="segments" />
    /// belong.
    /// </param>
    /// <param name="route">
    /// The route, which <see cref="Path">path</see> is being matched.
    /// </param>
    /// <returns>
    /// An instance of <see cref="IMatchResult" /> containing the consumed
    /// segments, and any positional parameters found during the match. Use its
    /// <see cref="IMatchResult.IsMatch">IsMatch</see> property to check if it
    /// was successful or not.
    /// </returns>
    /// <seealso cref="Match" />
    /// <seealso cref="NoMatch" />
    public delegate IMatchResult PathMatcher(
        ReadOnlyCollection<UrlSegment> segments,
        UrlSegmentGroup group,
        Route route);

    /// <summary>
    /// Interface representing the result of matching a route <see cref="Path" /> with
    /// segments (<see cref="UrlSegment" />) from a <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <seealso cref="PathMatcher" />
    public interface IMatchResult
    {
        /// <summary>
        /// Gets a value indicating whether the match was successful.
        /// </summary>
        /// <value>
        /// <c>true</c> if the match was successful.; <c>false</c> otherwise.
        /// </value>
        public bool IsMatch { get; }

        /// <summary>
        /// Gets the list of segments, from the <see cref="UrlSegmentGroup" />,
        /// consumed during the match.
        /// </summary>
        /// <value>The list of segments consumed during the match.</value>
        public List<UrlSegment> Consumed { get; }

        /// <summary>
        /// Gets a dictionary of the positional parameters found during the
        /// matching of a route's path to the segments in a <see cref="UrlSegmentGroup" />.
        /// </summary>
        /// <remarks>
        /// Positional parameters are specified in the route's <see cref="Path" />,
        /// and correspond to a path segment prefixed with the ':' character
        /// (example: in the path "/User/:id", ":id" represents a positional
        /// parameter with the name "id").
        /// </remarks>
        /// <value>
        /// A dictionary of positional parameters, where keys are the parameter
        /// names and values are the matching <see cref="UrlSegment" />.
        /// </value>
        public IDictionary<string, UrlSegment> PositionalParams { get; }
    }

    /// <summary>Gets the path matching method for this route.</summary>
    /// <value>
    /// A value from the <see cref="PathMatch" /> enum, indicating how to match
    /// the route's <see cref="Path" /> to the segment's <see cref="UrlSegment.Path" />
    /// .
    /// </value>
    public PathMatch MatchMethod { get; init; } = PathMatch.Default;

    /// <summary>Gets the path of the current route.</summary>
    /// <remarks>
    /// The path of a route cannot start with '/' and its value should be
    /// unique across all sibling routes. For child routes, the path cannot be
    /// relative.
    /// </remarks>
    /// <value>The path of the current route.</value>
    public string? Path { get; init; }

    /// <summary>
    /// Gets the custom <see cref="PathMatcher" /> to be used to match this
    /// route's <see cref="Path" /> to segments in the url.
    /// </summary>
    /// <remarks>
    /// If not explicitly set, the router uses a <see cref="DefaultMatcher" />
    /// that attempts to match the route's <see cref="Path" /> in accordance
    /// with what's specified in the route's <see cref="MatchMethod" />.
    /// </remarks>
    /// <value> A custom <see cref="PathMatcher" /> function.</value>
    public PathMatcher Matcher { get; init; } = DefaultMatcher;

    /// <summary>Gets the type of the view model for this route.</summary>
    /// <value>The type of the view model for this route.</value>
    public Type? ViewModelType { get; init; }

    /// <summary>Gets the outlet for which this route is specified.</summary>
    /// <value>
    /// An outlet name. Default is <see cref="UrlSegmentGroup.PrimaryOutlet" />.
    /// </value>
    public string Outlet { get; init; } = UrlSegmentGroup.PrimaryOutlet;

    /// <summary>Gets a collection of child routes, if any.</summary>
    /// <value>A collection of child routes, if any.</value>
    public Routes? Children { get; init; }

    /// <inheritdoc />
    public override string ToString()
    {
        var res = new StringBuilder();

        _ = res.Append(this.Outlet == UrlSegmentGroup.PrimaryOutlet ? "<pri>" : this.Outlet)
            .Append(':')
            .Append(
                this.Path != null
                    ? $"{this.Path}|{(this.MatchMethod == PathMatch.Full ? 'F' : 'P')}"
                    : "<matcher>");

        if (this.Children != null)
        {
            _ = res.Append('{').AppendJoin(", ", this.Children).Append('}');
        }

        return res.ToString();
    }

    /// <summary>
    /// Default <see cref="PathMatcher" /> used by the router to match a
    /// route's <see cref="Path" /> against segments in the url. Override this
    /// default matcher by setting the route's <see cref="Matcher" /> property.
    /// </summary>
    /// <inheritdoc cref="PathMatcher" />
    internal static IMatchResult DefaultMatcher(
        ReadOnlyCollection<UrlSegment> segments,
        UrlSegmentGroup group,
        Route route)
    {
        Debug.Assert(
            route.Path is not null,
            "when the default matcher is being used, a route must have a non-null path");

        var parts = route.Path.Split('/');

        if (parts.Length > segments.Count)
        {
            return new NoMatch();
        }

        if (route.MatchMethod == PathMatch.Full &&
            (group.Children is not { Count: 0 } || parts.Length < segments.Count))
        {
            // The route's Path is longer than the actual segments, or there
            // are more segments in the children, but we are looking for a full
            // match.
            return new NoMatch();
        }

        if (route.MatchMethod == PathMatch.StrictPrefix && parts.Length < segments.Count)
        {
            // The route's Path is longer than the actual segments, but we are
            // looking for a full match.
            return new NoMatch();
        }

        Dictionary<string, UrlSegment> posParams = [];

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
            else if (part != segment.Path)
            {
                return new NoMatch()
                {
                    Consumed = segments.GetRange(0, index),
                };
            }
        }

        return new Match()
        {
            Consumed = segments.GetRange(0, parts.Length),
            PositionalParams = posParams,
        };
    }

    /// <summary>
    /// Represents a successful match of a route's <see cref="Path" /> with
    /// segments (<see cref="UrlSegment" />) from a <see cref="UrlSegmentGroup" /> .
    /// </summary>
    /// <remarks>
    /// Instances of this class will always have their <see cref="IsMatch" />
    /// property be <c>true</c>, and will always have a non-empty collection of
    /// <see cref="Consumed" /> segments.
    /// </remarks>
    /// <seealso cref="PathMatcher" />
    public class Match : IMatchResult
    {
        /// <inheritdoc />
        public bool IsMatch => true;

        /// <inheritdoc />
        /// <remarks>
        /// All segments in the list have been successfully matched to a
        /// <see cref="UrlSegment" /> in the <see cref="UrlSegmentGroup" />.
        /// </remarks>
        public required List<UrlSegment> Consumed { get; init; } = [];

        /// <inheritdoc />
        public IDictionary<string, UrlSegment> PositionalParams { get; init; }
            = new Dictionary<string, UrlSegment>();
    }

    /// <summary>
    /// Represents a failed match of a route's <see cref="Path" /> with
    /// segments ( <see cref="UrlSegment" />) from a <see cref="UrlSegmentGroup" /> .
    /// </summary>
    /// <remarks>
    /// Instances of this class will always have their <see cref="IsMatch" />
    /// property be <c>false</c>, and will always have an empty collection of
    /// <see cref="PositionalParams" />. They may have segments in the
    /// <see cref="Consumed" /> collection, representing route segments that have
    /// been successfully matched up to until the overall match was deemed a
    /// failure.
    /// </remarks>
    /// <seealso cref="PathMatcher" />
    public class NoMatch : IMatchResult
    {
        /// <inheritdoc />
        public bool IsMatch => false;

        /// <inheritdoc />
        /// <remarks>
        /// Although the match failed, this list contains segments that have
        /// been successfully matched to a <see cref="UrlSegment" /> in the
        /// <see cref="UrlSegmentGroup" />, up to until the match failed.
        /// </remarks>
        public List<UrlSegment> Consumed { get; init; } = [];

        /// <inheritdoc />
        /// <remarks>
        /// When a match fails, the list of positional parameters is not
        /// updated, and therefore, it is always empty.
        /// </remarks>
        public IDictionary<string, UrlSegment> PositionalParams => ImmutableDictionary<string, UrlSegment>.Empty;
    }
}

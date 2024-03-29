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
/// Represents a single route definition including its corresponding view model
/// type, and child routes.
/// </summary>
public class Route : IRoute
{
    /// <inheritdoc />
    public PathMatch MatchMethod { get; init; } = PathMatch.Default;

    /// <inheritdoc />
    public string? Path { get; init; }

    /// <inheritdoc />
    /// <remarks>
    /// If not explicitly set, the router uses a <see cref="DefaultMatcher" />
    /// that attempts to match the route's <see cref="Path" /> in accordance
    /// with what's specified in the route's <see cref="MatchMethod" />.
    /// </remarks>
    public IRoute.PathMatcher Matcher { get; init; } = DefaultMatcher;

    /// <inheritdoc />
    public Type? ViewModelType { get; init; }

    /// <inheritdoc />
    public OutletName Outlet { get; init; } = OutletName.Primary;

    /// <inheritdoc />
    public IRoutes? Children { get; init; }

    /// <inheritdoc />
    public override string ToString()
    {
        var res = new StringBuilder();

        _ = res.Append(this.Outlet.IsPrimary ? "<pri>" : this.Outlet)
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
    /// Default path matcher used by the router to match a route's <see cref="Path" /> against segments in the url. Override this default
    /// matcher by setting the route's <see cref="Matcher" /> property.
    /// </summary>
    /// <inheritdoc cref="IRoute.PathMatcher" />
    internal static IMatchResult DefaultMatcher(
        ReadOnlyCollection<IUrlSegment> segments,
        IUrlSegmentGroup group,
        IRoute route)
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
    /// property be <see langword="true" />, and will always have a non-empty collection of
    /// <see cref="Consumed" /> segments.
    /// </remarks>
    /// <seealso cref="IRoute.PathMatcher" />
    public class Match : IMatchResult
    {
        /// <inheritdoc />
        public bool IsMatch => true;

        /// <inheritdoc />
        /// <remarks>
        /// All segments in the list have been successfully matched to a
        /// <see cref="UrlSegment" /> in the <see cref="UrlSegmentGroup" />.
        /// </remarks>
        public required IList<IUrlSegment> Consumed { get; init; } = [];

        /// <inheritdoc />
        public IDictionary<string, IUrlSegment> PositionalParams { get; init; }
            = new Dictionary<string, IUrlSegment>(StringComparer.Ordinal);
    }

    /// <summary>
    /// Represents a failed match of a route's <see cref="Path" /> with
    /// segments ( <see cref="UrlSegment" />) from a <see cref="UrlSegmentGroup" /> .
    /// </summary>
    /// <remarks>
    /// Instances of this class will always have their <see cref="IsMatch" />
    /// property be <see langword="false" />, and will always have an empty collection of
    /// <see cref="PositionalParams" />. They may have segments in the
    /// <see cref="Consumed" /> collection, representing route segments that have
    /// been successfully matched up to until the overall match was deemed a
    /// failure.
    /// </remarks>
    /// <seealso cref="IRoute.PathMatcher" />
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
        public IList<IUrlSegment> Consumed { get; init; } = [];

        /// <inheritdoc />
        /// <remarks>
        /// When a match fails, the list of positional parameters is not
        /// updated, and therefore, it is always empty.
        /// </remarks>
        public IDictionary<string, IUrlSegment> PositionalParams => ImmutableDictionary<string, IUrlSegment>.Empty;
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System;
using System.Collections.Immutable;
using System.Text;
using Destructurama.Attributed;
using DroidNet.Routing.Detail;

/// <summary>
/// Represents a single route definition including its corresponding view model
/// type, and child routes.
/// </summary>
public class Route : IRoute
{
    internal static readonly IRoute.PathMatcher DefaultMatcher = RouteMatcher.MatchRoute;

    /// <inheritdoc />
    public PathMatch MatchMethod { get; init; } = PathMatch.Prefix;

    /// <inheritdoc />
    public string? Path { get; init; }

    /// <inheritdoc />
    /// <remarks>
    /// If not explicitly set, the router uses a <see cref="RouteMatcher" /> that attempts to match the route's
    /// <see cref="Path" /> in accordance with what's specified in the route's <see cref="MatchMethod" />.
    /// </remarks>
    public IRoute.PathMatcher Matcher { get; init; } = DefaultMatcher;

    /// <inheritdoc />
    public Type? ViewModelType { get; init; }

    /// <inheritdoc />
    [LogAsScalar]
    public OutletName Outlet { get; init; } = OutletName.Primary;

    /// <inheritdoc />
    public IRoutes Children { get; init; } = new Routes([]);

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

        if (this.Children.Count != 0)
        {
            _ = res.Append('{').AppendJoin(", ", this.Children).Append('}');
        }

        return res.ToString();
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

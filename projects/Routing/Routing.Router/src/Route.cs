// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text;
using Destructurama.Attributed;
using DroidNet.Routing.Detail;

namespace DroidNet.Routing;

/// <summary>
/// Represents a single route definition including its corresponding view model
/// type, and child routes.
/// </summary>
public class Route : IRoute
{
    /// <summary>
    /// The default <see cref="IRoute.PathMatcher" /> used by the router.
    /// </summary>
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

    /// <inheritdoc />
    internal class Match : IMatchResult
    {
        /// <inheritdoc />
        public bool IsMatch => true;

        /// <inheritdoc />
        public required IReadOnlyList<IUrlSegment> Consumed { get; init; } = [];

        /// <inheritdoc />
        public IReadOnlyDictionary<string, IUrlSegment> PositionalParams { get; init; }
            = new Dictionary<string, IUrlSegment>(StringComparer.Ordinal);
    }

    /// <inheritdoc />
    internal class NoMatch : IMatchResult
    {
        /// <inheritdoc />
        public bool IsMatch => false;

        /// <inheritdoc />
        public IReadOnlyList<IUrlSegment> Consumed { get; init; } = [];

        /// <inheritdoc />
        public IReadOnlyDictionary<string, IUrlSegment> PositionalParams
            => ImmutableDictionary<string, IUrlSegment>.Empty;
    }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections.ObjectModel;

public interface IRoute
{
    /// <summary>
    /// Represents a function for matching a route against <see cref="IUrlSegment">segments</see> in a <see cref="IUrlSegmentGroup" />.
    /// </summary>
    /// <param name="segments">
    /// The list of segments, against which the route is to be matched. This could be a subset of the segments in the
    /// <paramref name="group" /> (typically, the remaining unmatched segments so far).
    /// </param>
    /// <param name="group">
    /// The <see cref="IUrlSegmentGroup" /> to which the <paramref name="segments" /> belong.
    /// </param>
    /// <param name="route">
    /// The route being matched.
    /// </param>
    /// <returns>
    /// An instance of <see cref="IMatchResult" /> containing the consumed segments, and any positional parameters found
    /// during the match. Use its <see cref="IMatchResult.IsMatch">IsMatch</see> property to check if it was successful
    /// or not.
    /// </returns>
    public delegate IMatchResult PathMatcher(
        ReadOnlyCollection<IUrlSegment> segments,
        IUrlSegmentGroup group,
        IRoute route);

    /// <summary>Gets the path matching method for this route.</summary>
    PathMatch MatchMethod { get; }

    /// <summary>Gets the path of the current route.</summary>
    /// <remarks>
    /// The path of a route is a URL path string, but it cannot start with '/' and its value should be unique across all
    /// sibling routes.
    /// </remarks>
    string? Path { get; }

    /// <summary>
    /// Gets the <see cref="PathMatcher" /> to be used to match this route's <see cref="Path" /> to segments in the url.
    /// </summary>
    /// <remarks>
    /// The router implementation has a default route matcher that uses the route's <see cref="Path" /> and <see cref="IRoute.MatchMethod" />. When a custom <see cref="PathMatcher" /> is set, the default matching rules are
    /// completely bypassed, the <see cref="Path" /> and <see cref="MatchMethod" /> no longer have a meaning for the route
    /// matching unless the custom matcher uses them.
    /// </remarks>
    PathMatcher Matcher { get; }

    /// <summary>Gets the type of the view model for this route.</summary>
    Type? ViewModelType { get; }

    /// <summary>Gets the outlet for which this route is specified.</summary>
    OutletName Outlet { get; }

    /// <summary>Gets the collection of child routes, if any.</summary>
    IRoutes? Children { get; }
}

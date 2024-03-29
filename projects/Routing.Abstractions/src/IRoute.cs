// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections.ObjectModel;

public interface IRoute
{
    /// <summary>
    /// Represents a function for matching a route's <see cref="Path">path</see>
    /// against <see cref="IUrlSegment">segments</see> in
    /// a <see cref="IUrlSegmentGroup" />.
    /// </summary>
    /// <param name="segments">
    /// The list of segments, against which the route's <see cref="Path">path</see>
    /// is to be matched. This could be a subset of the segments in the
    /// <paramref name="group" /> (typically, the remaining unmatched segments
    /// so far).
    /// </param>
    /// <param name="group">
    /// The <see cref="IUrlSegmentGroup" /> to which the <paramref name="segments" />
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
    public delegate IMatchResult PathMatcher(
        ReadOnlyCollection<IUrlSegment> segments,
        IUrlSegmentGroup group,
        IRoute route);

    /// <summary>Gets the path matching method for this route.</summary>
    /// <value>
    /// A value from the <see cref="PathMatch" /> enum, indicating how to match
    /// the route's <see cref="Path" /> to the segment's <see cref="IUrlSegment.Path" />
    /// .
    /// </value>
    PathMatch MatchMethod { get; }

    /// <summary>Gets the path of the current route.</summary>
    /// <remarks>
    /// The path of a route cannot start with '/' and its value should be
    /// unique across all sibling routes. For child routes, the path cannot be
    /// relative.
    /// </remarks>
    /// <value>The path of the current route.</value>
    string? Path { get; }

    /// <summary>
    /// Gets the custom <see cref="PathMatcher" /> to be used to match this
    /// route's <see cref="Path" /> to segments in the url.
    /// </summary>
    /// <value> A custom <see cref="PathMatcher" /> function.</value>
    PathMatcher Matcher { get; }

    /// <summary>Gets the type of the view model for this route.</summary>
    /// <value>The type of the view model for this route.</value>
    Type? ViewModelType { get; }

    /// <summary>Gets the outlet for which this route is specified.</summary>
    /// <value>
    /// An outlet name. Default is <see cref="OutletName.Primary" />.
    /// </value>
    OutletName Outlet { get; }

    /// <summary>Gets a collection of child routes, if any.</summary>
    /// <value>A collection of child routes, if any.</value>
    IRoutes? Children { get; }
}

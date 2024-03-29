// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Interface representing the result of matching a route <see cref="Path" /> with
/// segments (<see cref="IUrlSegment" />) from a <see cref="IUrlSegmentGroup" />.
/// </summary>
/// <seealso cref="IRoute.PathMatcher" />
public interface IMatchResult
{
    /// <summary>
    /// Gets a value indicating whether the match was successful.
    /// </summary>
    /// <value>
    /// <see langword="true" /> if the match was successful.; <see langword="false" /> otherwise.
    /// </value>
    public bool IsMatch { get; }

    /// <summary>
    /// Gets the list of segments, from the <see cref="IUrlSegmentGroup" />,
    /// consumed during the match.
    /// </summary>
    /// <value>The list of segments consumed during the match.</value>
    public IList<IUrlSegment> Consumed { get; }

    /// <summary>
    /// Gets a dictionary of the positional parameters found during the
    /// matching of a route's path to the segments in a <see cref="IUrlSegmentGroup" />.
    /// </summary>
    /// <remarks>
    /// Positional parameters are specified in the route's <see cref="Path" />,
    /// and correspond to a path segment prefixed with the ':' character
    /// (example: in the path "/User/:id", ":id" represents a positional
    /// parameter with the name "id").
    /// </remarks>
    /// <value>
    /// A dictionary of positional parameters, where keys are the parameter
    /// names and values are the matching <see cref="IUrlSegment" />.
    /// </value>
    public IDictionary<string, IUrlSegment> PositionalParams { get; }
}

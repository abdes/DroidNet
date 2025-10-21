// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents the result of matching URL segments against a route's path configuration.
/// </summary>
/// <remarks>
/// During route recognition, the router attempts to match URL segments against route
/// configurations. A match result indicates whether the match was successful, which URL segments
/// were consumed by the match, and what positional parameters were extracted (e.g., ':id' in
/// '/users/:id').
/// </remarks>
/// <seealso cref="IRoute.PathMatcher"/>
public interface IMatchResult
{
    /// <summary>
    /// Gets a value indicating whether the match was successful.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the route's path matched the URL segments;
    /// otherwise, <see langword="false"/>.
    /// </value>
    public bool IsMatch { get; }

    /// <summary>
    /// Gets the list of URL segments that were matched by the route.
    /// </summary>
    /// <value>
    /// The segments from the URL tree that were successfully matched and consumed during route
    /// recognition.
    /// </value>
    public IReadOnlyList<IUrlSegment> Consumed { get; }

    /// <summary>
    /// Gets the positional parameters extracted during route matching.
    /// </summary>
    /// <remarks>
    /// Positional parameters are specified in the route's path using ':' prefix (e.g., in
    /// '/users/:id', ':id' is a positional parameter). When matched, the actual URL segment value
    /// is captured in this dictionary.
    /// </remarks>
    /// <value>
    /// A dictionary mapping parameter names to their matched URL segments. For example, matching
    /// '/users/123' against '/users/:id' produces a parameter entry of "id" → segment "123".
    /// </value>
    public IReadOnlyDictionary<string, IUrlSegment> PositionalParams { get; }
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Enum representing different types of matching for routes.
/// </summary>
public enum PathMatch
{
    /// <summary>
    /// Default matching strategy when the <see cref="IRoute.MatchMethod" /> property
    /// was not explicitly set. Equivalent to <see cref="Prefix" />.
    /// </summary>
    Default = 0,

    /// <summary>
    /// Matches when the path starts with the route path.
    /// </summary>
    Prefix = 1,

    /// <summary>
    /// Matches when the path starts with the route path, but fails if the
    /// current segment group does not exactly match the route.
    /// </summary>
    /// <remarks>
    /// This is particularly useful for wide matching routes such as <c>""</c>,
    /// which may also require that no segments in the current segment group
    /// should remain after the match.
    /// </remarks>
    StrictPrefix = 2,

    /// <summary>
    /// Matches when the path is exactly equal to the route path.
    /// </summary>
    Full = 3,
}

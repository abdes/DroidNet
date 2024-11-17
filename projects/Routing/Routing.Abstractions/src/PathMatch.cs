// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Specifies how a route's path should be matched against URL segments during navigation.
/// </summary>
/// <remarks>
/// <para>
/// During route recognition, the router uses specified match strategy in a route, to determine how
/// strictly to match URL segments against the route's path. Prefix matching allows for partial
/// matches where the route path forms the beginning of the URL, while full matching requires exact
/// correspondence between the route's path and the URL segments.
/// </para>
/// <para>
/// For example, with a route path "/users", using <see cref="Prefix"/>, the URL "/users/123" would match,
/// but using <see cref="Full"/>, only the exact URL "/users" would match.
/// </para>
/// </remarks>
public enum PathMatch
{
    /// <summary>
    /// Represents an uninitialized or invalid matching strategy.
    /// </summary>
    /// <remarks>
    /// This value serves as a default when the enum is not explicitly set, helping detect
    /// configuration errors.
    /// </remarks>
    None = 0,

    /// <summary>
    /// Matches when the URL starts with the route's path.
    /// </summary>
    /// <remarks>
    /// Prefix matching is useful for routes with child routes. For example, a route with path
    /// "users" using prefix matching would match URLs like "users/123" or "users/settings",
    /// allowing child routes to handle the remaining segments.
    /// </remarks>
    Prefix = 1,

    /// <summary>
    /// Matches only when the URL exactly equals the route's path.
    /// </summary>
    /// <remarks>
    /// Full matching ensures strict correspondence between the URL and route path. This is
    /// particularly important for leaf routes or when matching empty paths, as an empty path
    /// would otherwise prefix-match any URL.
    /// </remarks>
    Full = 3,
}

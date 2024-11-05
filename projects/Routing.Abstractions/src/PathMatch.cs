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
    /// Matches when the path starts with the route path.
    /// </summary>
    Prefix = 1,

    /// <summary>
    /// Matches when the path is exactly equal to the route path.
    /// </summary>
    Full = 3,
}

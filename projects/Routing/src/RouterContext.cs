// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using DroidNet.Routing.Contracts;

/// <summary>
/// Represents the context in which a navigation is taking place.
/// </summary>
/// <param name="target">
/// The name of the navigation target where the root content should be
/// loaded.
/// </param>
/// <seealso cref="Target" />
public class RouterContext(Target target)
{
    /// <summary>Gets or sets the router's internal state for this navigation context.</summary>
    /// <value>The internal <see cref="RouterState" /> for this context.</value>
    internal IRouterState? State { get; set; }

    /// <summary>
    /// Gets the name of the navigation target where the root content should be
    /// loaded.
    /// </summary>
    /// <value>
    /// The name of the navigation target where the root content should be
    /// loaded.
    /// </value>
    protected Target Target { get; } = target;
}

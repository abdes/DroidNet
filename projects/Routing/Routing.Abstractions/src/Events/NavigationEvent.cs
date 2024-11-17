// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents a base class for all navigation-related events in the routing system.
/// </summary>
/// <remarks>
/// Navigation events track router state changes during URL-based navigation and route activation.
/// </remarks>
/// <param name="options">The navigation options in effect when the event occurred.</param>
public class NavigationEvent(NavigationOptions options) : RouterEvent
{
    /// <summary>
    /// Gets the navigation options that were used during navigation.
    /// </summary>
    public NavigationOptions Options => options;
}

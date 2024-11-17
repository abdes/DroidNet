// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents a base class for all context-related events in the routing system.
/// </summary>
/// <remarks>
/// Context events track changes to navigation contexts during router operation.
/// </remarks>
/// <param name="context">The navigation context associated with this event, or null if no context applies.</param>
public abstract class RouterContextEvent(INavigationContext? context) : RouterEvent
{
    /// <summary>
    /// Gets the navigation context associated with the current navigation.
    /// </summary>
    public INavigationContext? Context { get; } = context;

    /// <inheritdoc />
    public override string ToString() => this.Context?.ToString() ?? "_None_";
}

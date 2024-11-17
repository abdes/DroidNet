// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Event arguments for router context-related events.
/// </summary>
/// <param name="context">The navigation context associated with the event, or null if no context applies.</param>
public class ContextEventArgs(INavigationContext? context) : EventArgs
{
    /// <summary>
    /// Gets the navigation context associated with this event.
    /// </summary>
    public INavigationContext? Context => context;
}

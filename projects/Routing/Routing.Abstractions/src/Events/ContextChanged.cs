// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when the active navigation context changes.
/// </summary>
/// <remarks>
/// Signals that the router has switched to a different navigation context, which may have different
/// router state and outlet containers.
/// </remarks>
/// <param name="context">The new active navigation context, or null if no context is active.</param>
public class ContextChanged(INavigationContext? context) : RouterContextEvent(context)
{
    /// <inheritdoc />
    public override string ToString() => $"Context changed -> {base.ToString()}";
}

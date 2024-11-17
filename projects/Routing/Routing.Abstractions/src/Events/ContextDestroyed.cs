// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when a navigation context is destroyed.
/// </summary>
/// <remarks>
/// Signals that the router has destroyed a context, typically when its associated navigation target
/// is no longer valid or needed.
/// </remarks>
/// <param name="context">The navigation context that was destroyed.</param>
public class ContextDestroyed(INavigationContext context) : RouterContextEvent(context)
{
    /// <inheritdoc />
    public override string ToString() => $"Context destroyed: {base.ToString()}";
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when a new navigation context is created.
/// </summary>
/// <remarks>
/// Signals that the router has created a new context for handling navigation within a specific target.
/// </remarks>
/// <param name="context">The newly created navigation context.</param>
public class ContextCreated(INavigationContext context) : RouterContextEvent(context)
{
    /// <inheritdoc />
    public override string ToString() => $"Context created: {base.ToString()}";
}

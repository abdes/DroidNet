// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Navigation options for performing targeted updates to the router state.
/// </summary>
/// <remarks>
/// <para>
/// Partial navigation enables precise updates to specific portions of the router state without
/// affecting the entire navigation tree. By providing a reference route as context, developers can
/// use either relative URLs or direct state modifications to update only the relevant parts of their
/// application, resulting in more efficient updates and better performance.
/// </para>
/// <para>
/// This targeted approach is particularly valuable in complex applications where different regions
/// operate independently. For instance, updating a details panel shouldn't require rebuilding the
/// entire navigation state, and partial navigation makes this possible while maintaining the
/// application's navigational consistency.
/// </para>
/// </remarks>
public class PartialNavigation : NavigationOptions
{
    /// <summary>
    /// Gets or initializes the reference route for partial navigation.
    /// </summary>
    /// <value>
    /// The route that provides context for resolving relative URLs and determining where state
    /// changes should be applied. Unlike full navigation where this is optional, partial navigation
    /// requires this context to properly scope its updates.
    /// </value>
    public override required IActiveRoute? RelativeTo { get; init; }
}

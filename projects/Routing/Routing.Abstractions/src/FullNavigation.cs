// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Navigation options for absolute URL-based navigation that creates a new router state.
/// </summary>
/// <remarks>
/// <para>
/// Full navigation represents a complete state transition in the routing system. Using an absolute URL,
/// it creates an entirely new router state, clearing any existing state and building a fresh view model
/// hierarchy. This makes it ideal for major navigational changes or initial application state setup.
/// </para>
/// <para>
/// The router matches the URL against its route configuration to construct a complete state tree.
/// This comprehensive approach ensures a clean, predictable state that exactly matches the URL,
/// preventing any inconsistencies that might arise from incremental updates.
/// </para>
/// </remarks>
public class FullNavigation : NavigationOptions
{
    /// <summary>
    /// Gets a value indicating whether the source target should be closed when this navigation is activated.
    /// </summary>
    /// <value>
    /// True if the source target (typically <see cref="Target.Self"/>) should be closed when the new
    /// target is activated in a multi-window scenario. False if no target should be closed during activation.
    /// </value>
    public bool ReplaceTarget { get; init; }
}

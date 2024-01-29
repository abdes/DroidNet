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
public class RouterContext(string target)
{
    /// <summary>
    /// Gets the name of the navigation target where the root content should be
    /// loaded.
    /// </summary>
    /// <value>
    /// The name of the navigation target where the root content should be
    /// loaded.
    /// </value>
    /// <remarks>
    /// <para>
    /// The following target keywords have special meanings for where to load
    /// the content:
    /// </para>
    /// <list>
    /// <item>
    /// <term>_main</term> loads the content into the top level main context
    /// (typically the application's main window). This special target name
    /// must always be valid.
    /// </item>
    /// <item>
    /// <term>_self</term> the current context. (Default)
    /// </item>
    /// </list>
    /// </remarks>
    public string Target { get; } = target;

    /// <summary>Gets or sets the router's internal state for this navigation context.</summary>
    /// <value>The internal <see cref="RouterState" /> for this context.</value>
    internal IRouterState? State { get; set; }
}

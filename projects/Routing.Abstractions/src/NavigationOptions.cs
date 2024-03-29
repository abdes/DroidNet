// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Navigation options passed to the <see cref="IRouter">router</see> when
/// requesting a navigation.
/// </summary>
/// <seealso cref="FullNavigation" />
/// <seealso cref="PartialNavigation" />
public abstract class NavigationOptions
{
    /// <summary>Gets the target of the navigation.</summary>
    /// <value>
    /// A string identifying the navigation target. Can refer to one of the
    /// special <see cref="Target">targets</see> or to a custom one. In
    /// both cases, the value should be a key to an appropriate object
    /// registered with the dependency injector.
    /// </value>
    public Target? Target { get; init; }

    /// <summary>
    /// Gets the <see cref="IActiveRoute" />, relative to which the navigation
    /// will happen.
    /// </summary>
    /// <value>
    /// When not <see langword="null" />, it contains the active route relative to which the
    /// url tree for navigation will be resolved before navigation starts.
    /// </value>
    public virtual IActiveRoute? RelativeTo { get; init; }

    /// <summary>
    /// Gets additional information passed to the navigation request, and which
    /// will be made available to the navigation events callbacks when they are
    /// invoked. This data is completely opaque to the router.
    /// </summary>
    public object? AdditionalInfo { get; init; }
}

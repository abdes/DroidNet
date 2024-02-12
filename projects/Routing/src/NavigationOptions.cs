// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using DroidNet.Routing.Contracts;

/// <summary>
/// Represents options used when requesting the <see cref="Router" /> to
/// navigate.
/// </summary>
public class NavigationOptions
{
    /// <summary>Gets the target of the navigation.</summary>
    /// <value>
    /// A string identifying the navigation target. Can refer to one of the
    /// special <see cref="RouterContext">targets</see> or to a custom one. In
    /// both cases, the value should be a key to an appropriate object
    /// registered with the dependency injector.
    /// </value>
    public Target? Target { get; init; }

    /// <summary>
    /// Gets the <see cref="IActiveRoute" />, relative to which the navigation
    /// will happen.
    /// </summary>
    /// <value>
    /// When not <c>null</c>, it contains the active route relative to which the
    /// url tree for navigation will be resolved before navigation starts.
    /// </value>
    public IActiveRoute? RelativeTo { get; init; }
}

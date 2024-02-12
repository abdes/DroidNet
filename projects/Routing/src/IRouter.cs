// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using DroidNet.Routing.Contracts;
using DroidNet.Routing.Events;

public interface IRouter
{
    /// <summary>Gets the routes configuration for this router.</summary>
    /// <value>
    /// A <see cref="Routes" /> configuration, usually injected into the Router
    /// via the dependency injector.
    /// </value>
    Routes Config { get; }

    IObservable<RouterEvent> Events { get; }

    UrlTree? GetCurrentUrlTreeForTarget(string target);

    /// <summary>Navigates to the specified URL.</summary>
    /// <param name="url">
    /// The URL to navigate to. Represents an absolute URL if it starts with
    /// <c>'/'</c>, or a relative one if not.
    /// </param>
    /// <param name="options">
    /// The navigation options. When <c>null</c>, default
    /// <see cref="NavigationOptions" /> are used.
    /// </param>
    /// <remarks>
    /// TODO(abdes): describe the details of the navigation process.
    /// </remarks>
    void Navigate(string url, NavigationOptions? options = null);

    IActiveRoute? GetCurrentStateForTarget(string target);
}

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using DroidNet.Routing.Contracts;

/// <summary>
/// Represents the current state of the application router.
/// </summary>
/// <param name="url">
/// The navigation url used to create this router state.
/// </param>
/// <param name="urlTree">
/// The parsed <see cref="UrlTree" /> corresponding to the <paramref name="url" />.
/// </param>
/// <param name="root">The root <see cref="IActiveRoute" /> of the state.</param>
internal class RouterState(string url, UrlTree urlTree, ActiveRoute root) : IRouterState
{
    public ActiveRoute Root { get; } = root;

    public string Url { get; } = url;

    public UrlTree UrlTree { get; } = urlTree;

    IActiveRoute IRouterState.Root => this.Root;
}

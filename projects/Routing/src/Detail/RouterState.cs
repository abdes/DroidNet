// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

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
internal sealed class RouterState(string url, IUrlTree urlTree, IActiveRoute root) : IRouterState
{
    public string Url { get; set; } = url;

    public IActiveRoute RootNode { get; } = root;

    public IUrlTree UrlTree { get; } = urlTree;
}

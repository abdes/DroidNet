// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

public class ActivationStarted(NavigationOptions options, IRouterState state) : NavigationEvent(options)
{
    public IRouterState RouterState { get; } = state;

    /// <inheritdoc />
    public override string ToString() => $"Routes activation started -> {this.RouterState.RootNode}";
}

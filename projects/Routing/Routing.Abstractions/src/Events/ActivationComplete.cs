// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

public class ActivationComplete(NavigationOptions options, INavigationContext context) : NavigationEvent(options)
{
    public INavigationContext Context { get; } = context;

    /// <inheritdoc />
    public override string ToString() => $"Routes activation complete -> {this.Context.State?.RootNode}";
}

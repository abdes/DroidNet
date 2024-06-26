// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

public abstract class RouterContextEvent(IRouterContext? context) : RouterEvent
{
    public IRouterContext? Context { get; } = context;

    /// <inheritdoc />
    public override string ToString() => this.Context?.ToString() ?? "_None_";
}

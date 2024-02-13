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

public class ContextChanged(IRouterContext? context) : RouterContextEvent(context)
{
    /// <inheritdoc />
    public override string ToString() => $"Context changed -> {base.ToString()}";
}

public class ContextDestroyed(IRouterContext context) : RouterContextEvent(context)
{
    /// <inheritdoc />
    public override string ToString() => $"Context destroyed: {base.ToString()}";
}

public class ContextCreated(IRouterContext context) : RouterContextEvent(context)
{
    /// <inheritdoc />
    public override string ToString() => $"Context created: {base.ToString()}";
}

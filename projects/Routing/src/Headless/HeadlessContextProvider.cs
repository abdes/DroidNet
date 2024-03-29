// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Headless;

using System;

internal sealed class HeadlessContextProvider : IContextProvider
{
    public HeadlessContextProvider()
    {
        // TODO(abdes): temporarily until the headless is implemented
        _ = this.ContextChanged;
        _ = this.ContextCreated;
        _ = this.ContextDestroyed;
    }

    public event EventHandler<ContextEventArgs>? ContextChanged;

    public event EventHandler<ContextEventArgs>? ContextCreated;

    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    public void ActivateContext(RouterContext context) => throw new NotImplementedException();

    public RouterContext ContextForTarget(Target target, RouterContext? currentContext = null)
        => throw new NotImplementedException();
}

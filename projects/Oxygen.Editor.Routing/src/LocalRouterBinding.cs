// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;

namespace Oxygen.Editor.Routing;

/// <summary>
///     Makes a scope's router the <see cref="LocalRouterContext.LocalRouter" /> of the scope's local
///     router context. <see cref="RouterScopeModule" /> activates it when the scope opens.
/// </summary>
internal sealed class LocalRouterBinding
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="LocalRouterBinding" /> class.
    /// </summary>
    /// <param name="context">The scope's local router context.</param>
    /// <param name="router">The scope's router.</param>
    public LocalRouterBinding(LocalRouterContext context, IRouter router)
    {
        ArgumentNullException.ThrowIfNull(context);
        context.LocalRouter = router;
    }
}

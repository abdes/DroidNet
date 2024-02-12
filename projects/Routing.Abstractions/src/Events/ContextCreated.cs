// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

public class ContextCreated(IRouterContext context) : RouterContextEvent(context)
{
    /// <inheritdoc />
    public override string ToString() => $"Context created: {base.ToString()}";
}

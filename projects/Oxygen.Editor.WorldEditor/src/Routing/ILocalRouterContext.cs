// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;

namespace Oxygen.Editor.WorldEditor.Routing;

public interface ILocalRouterContext : INavigationContext
{
    public object RootViewModel { get; }

    public IRouter LocalRouter { get; }

    public IRouter ParentRouter { get; }
}

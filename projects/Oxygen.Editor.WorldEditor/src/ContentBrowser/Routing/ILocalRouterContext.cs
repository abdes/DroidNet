// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser.Routing;

using DroidNet.Routing;

public interface ILocalRouterContext : IRouterContext
{
    public ContentBrowserViewModel RootViewModel { get; }

    public IRouter Router { get; }

    public IRouter GlobalRouter { get; }
}

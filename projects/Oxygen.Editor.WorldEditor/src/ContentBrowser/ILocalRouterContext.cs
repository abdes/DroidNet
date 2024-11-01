// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using DroidNet.Routing;

internal interface ILocalRouterContext : IRouterContext
{
    public ContentBrowserViewModel RootViewModel { get; }

    public IRouter Router { get; }
}

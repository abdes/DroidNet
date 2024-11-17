// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// A styled component for the body part of a tree item.
/// </summary>
public partial class TreeItemBody : ContentPresenter
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TreeItemBody"/> class.
    /// </summary>
    public TreeItemBody()
    {
        this.Style = (Style)Application.Current.Resources[nameof(TreeItemBody)];
    }
}

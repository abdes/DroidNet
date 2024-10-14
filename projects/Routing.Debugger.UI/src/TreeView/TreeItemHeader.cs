// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A styled component for the header part of a tree item.
/// </summary>
public partial class TreeItemHeader : ContentPresenter
{
    public TreeItemHeader() => this.Style = (Style)Application.Current.Resources[nameof(TreeItemHeader)];
}

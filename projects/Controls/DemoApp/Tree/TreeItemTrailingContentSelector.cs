// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Tree.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>Selects the lock action for demo entity rows, leaving the scene root without trailing content.</summary>
internal sealed partial class TreeItemTrailingContentSelector : DataTemplateSelector
{
    /// <summary>Gets or sets the generic item-lock action used by entity nodes.</summary>
    public DataTemplate? EntityActionTemplate { get; set; }

    /// <inheritdoc />
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item is EntityAdapter ? this.EntityActionTemplate : null;
}

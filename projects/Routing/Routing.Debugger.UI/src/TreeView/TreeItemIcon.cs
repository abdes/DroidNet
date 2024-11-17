// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// A styled <see cref="ImageIcon" /> for the icon used in tree items.
/// </summary>
public partial class TreeItemIcon : ImageIcon
{
    /// <summary>
    /// Identifies the <see cref="Item"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ItemProperty = DependencyProperty.Register(
        nameof(Item),
        typeof(ITreeItem),
        typeof(TreeItemIcon),
        new PropertyMetadata(default(ITreeItem), OnItemChanged));

    /// <summary>
    /// Initializes a new instance of the <see cref="TreeItemIcon"/> class.
    /// </summary>
    public TreeItemIcon()
    {
        this.Style = (Style)Application.Current.Resources[nameof(TreeItemIcon)];
    }

    /// <summary>
    /// Gets or sets the tree item associated with this icon.
    /// </summary>
    /// <value>The tree item associated with this icon.</value>
    public ITreeItem Item
    {
        get => (ITreeItem)this.GetValue(ItemProperty);
        set => this.SetValue(ItemProperty, value);
    }

    /// <summary>
    /// Handles changes to the <see cref="Item"/> property.
    /// </summary>
    /// <param name="d">The dependency object on which the property changed.</param>
    /// <param name="args">Event data for the property change.</param>
    private static void OnItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
    {
        var control = (TreeItemIcon)d;
        var item = (ITreeItem)args.NewValue;
        if (item == null)
        {
            return;
        }

        control.Source = (ImageSource)Application.Current.Resources[$"{item.GetType().Name}.Icon"];
        control.Visibility = item.IsRoot ? Visibility.Collapsed : Visibility.Visible;
    }
}

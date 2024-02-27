// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

public class TreeItemIcon : ImageIcon
{
    public static readonly DependencyProperty ItemProperty = DependencyProperty.Register(
        nameof(Item),
        typeof(ITreeItem),
        typeof(TreeItemIcon),
        new PropertyMetadata(default(ITreeItem), OnItemChanged));

    public TreeItemIcon() => this.Style = (Style)Application.Current.Resources[nameof(TreeItemIcon)];

    public ITreeItem Item
    {
        get => (ITreeItem)this.GetValue(ItemProperty);
        set => this.SetValue(ItemProperty, value);
    }

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

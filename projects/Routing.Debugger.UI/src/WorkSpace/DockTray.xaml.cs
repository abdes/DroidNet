// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Routing.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// A custom control to display the collection of dockables in minimized docks
/// as in a side tray.
/// </summary>
[ViewModel(typeof(DockTrayViewModel))]
public sealed partial class DockTray
{
    public DockTray() => this.InitializeComponent();

    private void ItemsView_OnItemInvoked(ItemsView sender, ItemsViewItemInvokedEventArgs args)
    {
        _ = sender;
        this.ViewModel.ShowDockableCommand.Execute(args.InvokedItem);
    }
}

internal sealed class OrientationTemplateSelector : DataTemplateSelector
{
    public DataTemplate? VerticalTemplate { get; set; }

    public DataTemplate? HorizontalTemplate { get; set; }

    public Orientation DockTrayOrientation { get; set; }

    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => this.DockTrayOrientation == Orientation.Vertical
            ? this.VerticalTemplate
            : this.HorizontalTemplate;
}

internal sealed class OrientationToLayoutConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        var orientation = (Orientation)value;
        return new StackLayout()
        {
            Orientation = orientation,
            Spacing = 8,
        };
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}

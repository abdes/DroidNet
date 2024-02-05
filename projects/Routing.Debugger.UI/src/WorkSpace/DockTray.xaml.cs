// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Collections.ObjectModel;
using DroidNet.Docking;
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
    public DockTray(ReadOnlyObservableCollection<IDock> docks, Orientation orientation)
    {
        this.InitializeComponent();

        this.ViewModel = new DockTrayViewModel(docks);
        this.Orientation = orientation;
    }

    public Orientation Orientation { get; }
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
        => throw new NotImplementedException();
}

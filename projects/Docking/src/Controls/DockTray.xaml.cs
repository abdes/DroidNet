// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Docking.Controls;

/// <summary>
/// A custom control to display the collection of dockables in minimized docks
/// as in a side tray.
/// </summary>
[ViewModel(typeof(DockTrayViewModel))]
public sealed partial class DockTray
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DockTray"/> class.
    /// </summary>
    public DockTray()
    {
        this.InitializeComponent();
    }

    private void ItemsView_OnItemInvoked(ItemsView sender, ItemsViewItemInvokedEventArgs args)
    {
        _ = sender;
        this.ViewModel?.ShowDockableCommand.Execute(args.InvokedItem);
    }
}

/// <summary>
/// Selects a <see cref="DataTemplate" /> based on the orientation of the dock tray.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "StyleCop.CSharp.MaintainabilityRules",
    "SA1402:File may only contain a single type",
    Justification = "this class is only used for the DockTray. It is 'internal' because of the XAML generated code ")]
internal sealed partial class OrientationTemplateSelector : DataTemplateSelector
{
    /// <summary>
    /// Gets or sets the template to use when the orientation is vertical.
    /// </summary>
    public DataTemplate? VerticalTemplate { get; set; }

    /// <summary>
    /// Gets or sets the template to use when the orientation is horizontal.
    /// </summary>
    public DataTemplate? HorizontalTemplate { get; set; }

    /// <summary>
    /// Gets or sets the orientation of the dock tray.
    /// </summary>
    public Orientation DockTrayOrientation { get; set; }

    /// <summary>
    /// Selects a template based on the orientation of the dock tray.
    /// </summary>
    /// <param name="item">
    /// The data object for which to select the template.
    /// </param>
    /// <param name="container">
    /// The parent container for the templated item.
    /// </param>
    /// <returns>
    /// A <see cref="DataTemplate" /> that matches the current orientation.
    /// </returns>
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => this.DockTrayOrientation == Orientation.Vertical
            ? this.VerticalTemplate
            : this.HorizontalTemplate;
}

/// <summary>
/// A converter that converts an <see cref="Orientation" /> to the corresponding <see cref="StackLayout" />.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "StyleCop.CSharp.MaintainabilityRules",
    "SA1402:File may only contain a single type",
    Justification = "this class is only used for the DockTray. It is 'internal' because of the XAML generated code ")]
internal sealed partial class OrientationToLayoutConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        var orientation = (Orientation)value;
        return new StackLayout()
        {
            Orientation = orientation,
            Spacing = 8,
        };
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}

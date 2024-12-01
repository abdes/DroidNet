// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Docking.Controls;

/// <summary>
/// A container control to hold the tabs for dockables in a dock panel.
/// </summary>
public partial class DockableTabsBar
{
    /// <summary>
    /// Identifies the <see cref="Dockables"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty DockablesProperty =
        DependencyProperty.Register(
            nameof(Dockables),
            typeof(ReadOnlyObservableCollection<IDockable>),
            typeof(DockableTabsBar),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Identifies the <see cref="ActiveDockable"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ActiveDockableProperty =
        DependencyProperty.Register(
            nameof(ActiveDockable),
            typeof(IDockable),
            typeof(DockableTabsBar),
            new PropertyMetadata(defaultValue: null, OnActiveDockableChanged));

    /// <summary>
    /// Identifies the <see cref="IconConverter"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IconConverterProperty =
        DependencyProperty.Register(
            nameof(IconConverter),
            typeof(IValueConverter),
            typeof(DockableTabsBar),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Identifies the <see cref="IsCompact"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsCompactProperty =
         DependencyProperty.Register(
             nameof(IsCompact),
             typeof(bool),
             typeof(DockableTabsBar),
             new PropertyMetadata(defaultValue: false));

    /// <summary>
    /// Gets or sets <see cref="ReadOnlyObservableCollection{IDockable}">collection</see> of dockable entities.
    /// </summary>
    public ReadOnlyObservableCollection<IDockable> Dockables
    {
        get => (ReadOnlyObservableCollection<IDockable>)this.GetValue(DockablesProperty);
        set => this.SetValue(DockablesProperty, value);
    }

    /// <summary>
    /// Gets or sets the currently active dockable entity. When this property is set, the
    /// corresponding tab is selected and any other previously selected tab is deselected.
    /// </summary>
    public IDockable ActiveDockable
    {
        get => (IDockable)this.GetValue(ActiveDockableProperty);
        set => this.SetValue(ActiveDockableProperty, value);
    }

    /// <summary>
    /// Gets or sets the converter used to get an icon for the dockable entity. This value is
    /// propagated to all tabs inside this tabs bar.
    /// </summary>
    /// <remark>
    /// If this property is not set, a default icon is automatically set for each tab.
    /// </remark>
    public IValueConverter IconConverter
    {
        get => (IValueConverter)this.GetValue(IconConverterProperty);
        set => this.SetValue(IconConverterProperty, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the tabs bar is in compact mode. This value is
    /// propagated to all tabs inside this tabs bar.
    /// </summary>
    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    /// <summary>
    /// Called when the <see cref="ActiveDockable"/> property changes.
    /// </summary>
    /// <param name="d">The dependency object on which the property changed.</param>
    /// <param name="args">The event data for the property change.</param>
    private static void OnActiveDockableChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
    {
        var control = (DockableTabsBar)d;
        control.UpdateSelectionStates();
    }

    /// <summary>
    /// Called when the <see cref="IconConverter"/> property changes.
    /// </summary>
    /// <param name="d">The dependency object on which the property changed.</param>
    /// <param name="args">The event data for the property change.</param>
    private static void OnIconConverterChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
    {
        var control = (DockableTabsBar)d;
        control.OnIconConverterChanged((IValueConverter)args.NewValue);
    }
}

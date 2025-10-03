// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
///     Represents an individual menu item control that renders a four-column layout with Icon, Text, Accelerator, and State.
/// </summary>
[SuppressMessage(
    "ReSharper",
    "ClassWithVirtualMembersNeverInherited.Global",
    Justification = "class is designed to be extended when needed")]
public partial class MenuItem
{
    /// <summary>
    ///     The backing <see cref="DependencyProperty" /> for the <see cref="ItemData" /> property.
    /// </summary>
    public static readonly DependencyProperty ItemDataProperty = DependencyProperty.Register(
        nameof(ItemData),
        typeof(MenuItemData),
        typeof(MenuItem),
        new PropertyMetadata(
            defaultValue: null,
            (d, e) => ((MenuItem)d).OnItemDataChanged((MenuItemData?)e.OldValue, (MenuItemData?)e.NewValue)));

    /// <summary>
    ///     Identifies the <see cref="ShowSubmenuGlyph"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ShowSubmenuGlyphProperty = DependencyProperty.Register(
        nameof(ShowSubmenuGlyph),
        typeof(bool),
        typeof(MenuItem),
        new PropertyMetadata(true, OnShowSubmenuGlyphChanged));

    /// <summary>
    ///     Gets or sets the menu item data that provides the content and behavior for this control.
    /// </summary>
    /// <value>
    ///     A <see cref="MenuItemData" /> instance that contains the text, icon, command, and other
    ///     properties needed to render and interact with the menu item.
    /// </value>
    /// <remarks>
    ///     The <see cref="ItemData" /> property serves as the data source for the menu item control.
    ///     When this property changes, the control updates its visual state, text content, icon,
    ///     and other display elements to reflect the new data. The control also registers for
    ///     property change notifications from the data object to stay synchronized.
    /// </remarks>
    public MenuItemData? ItemData
    {
        get => (MenuItemData?)this.GetValue(ItemDataProperty);
        set => this.SetValue(ItemDataProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether submenu arrow glyphs should be shown when the
    ///     underlying data has children. Menu bar root items typically suppress the glyph.
    /// </summary>
    public bool ShowSubmenuGlyph
    {
        get => (bool)this.GetValue(ShowSubmenuGlyphProperty);
        set => this.SetValue(ShowSubmenuGlyphProperty, value);
    }

    /// <summary>
    ///     Handles changes to the <see cref="ItemData" /> property.
    /// </summary>
    /// <param name="oldData">The previous value of the <see cref="ItemData" /> property.</param>
    /// <param name="newData">The new value of the <see cref="ItemData" /> property.</param>
    /// <remarks>
    ///     This method is called whenever the <see cref="ItemData" /> property changes. It
    ///     un-registers property change handlers from the old data and registers handlers with the
    ///     new data. It also updates all visual states to reflect the new data values.
    /// </remarks>
    protected virtual void OnItemDataChanged(MenuItemData? oldData, MenuItemData? newData)
    {
        // Un-register event handlers from the old data if any
        if (oldData is not null)
        {
            oldData.PropertyChanged -= this.ItemData_OnPropertyChanged;
        }

        // Register event handlers with the new data and update visual states
        if (newData is not null)
        {
            newData.PropertyChanged += this.ItemData_OnPropertyChanged;

            // Update visual states on the UI thread
            _ = this.DispatcherQueue.TryEnqueue(this.UpdateAllVisualStates);
        }
        else
        {
            // Clear visual states when no data is present
            _ = this.DispatcherQueue.TryEnqueue(this.UpdateAllVisualStates);
        }
    }

    private static void OnShowSubmenuGlyphChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is MenuItem menuItem)
        {
            menuItem.UpdateCheckmarkVisualState();
        }
    }

    private void ItemData_OnPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is not MenuItemData data)
        {
            return;
        }

        // Update visual states based on which property changed
        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            switch (e.PropertyName)
            {
                case nameof(MenuItemData.IsEnabled):
                    this.UpdateInteractionVisualState();
                    break;

                case nameof(MenuItemData.IsActive):
                    this.UpdateInteractionVisualState();
                    this.UpdateActiveVisualState();
                    break;

                case nameof(MenuItemData.Icon):
                    this.UpdateIconVisualState();
                    break;

                case nameof(MenuItemData.AcceleratorText):
                    this.UpdateAcceleratorVisualState();
                    break;

                case nameof(MenuItemData.IsSeparator):
                    this.UpdateTypeVisualState();
                    break;

                case nameof(MenuItemData.SubItems):
                case nameof(MenuItemData.IsCheckable):
                case nameof(MenuItemData.IsChecked):
                case nameof(MenuItemData.RadioGroupId):
                    this.UpdateCheckmarkVisualState();
                    break;

                default:
                    // For other properties (Text, Command, etc.), no visual state update needed
                    // The binding will handle text updates automatically
                    break;
            }
        });
    }
}

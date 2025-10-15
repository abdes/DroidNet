// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents an individual menu item control, used within a <see cref="MenuBar"/> or cascaded menu flyouts.
/// </summary>
public partial class MenuItem
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(MenuItem),
        new PropertyMetadata(defaultValue: null));

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
        new PropertyMetadata(defaultValue: true, OnShowSubmenuGlyphChanged));

    /// <summary>
    ///     Gets or sets the menu source that provides shared services for the menu system.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

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

    private static void OnShowSubmenuGlyphChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is MenuItem { ItemData: { } } menuItem)
        {
            menuItem.UpdateCheckmarkVisualState();
        }
    }

    /// <summary>
    ///     Responds to changes of the <see cref="ItemData"/> dependency property.
    /// </summary>
    /// <param name="oldData">The previously attached <see cref="MenuItemData"/>, or <see langword="null"/>.</param>
    /// <param name="newData">The newly attached <see cref="MenuItemData"/>, or <see langword="null"/> if cleared.</param>
    /// <remarks>
    ///     Rewires <see cref="INotifyPropertyChanged.PropertyChanged"/> from the old data to the new data and schedules
    ///     a refresh of all visual states on the UI dispatcher. The method is <see langword="virtual"/>; when
    ///     overriding, preserve the subscription semantics or call the base implementation.
    /// </remarks>
    private void OnItemDataChanged(MenuItemData? oldData, MenuItemData? newData)
    {
        // Safely rewire PropertyChanged from old to new data
        oldData?.PropertyChanged -= this.ItemData_OnPropertyChanged;
        newData?.PropertyChanged += this.ItemData_OnPropertyChanged;

        // Update access key immediately so the control participates in AccessKeyManager.
        this.AccessKey = newData?.Mnemonic?.ToString() ?? string.Empty;

        // Only queue visual state updates if template is already applied.
        // Otherwise, OnApplyTemplate will handle initialization to avoid duplicate updates.
        if (this.IsTemplateApplied)
        {
            _ = this.DispatcherQueue.TryEnqueue(() =>
            {
                this.UpdateTypeVisualState();
                this.UpdateCommonVisualState();
                this.UpdateIconVisualState();
                this.UpdateAcceleratorVisualState();
                this.UpdateCheckmarkVisualState();

                this.RefreshTextPresentation();
            });
        }
    }

    private void ItemData_OnPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        Debug.Assert(sender is MenuItemData, "Expecting sender to be MenuItemData");

        // Update visual states based on which property changed
        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            switch (e.PropertyName)
            {
                case nameof(MenuItemData.IsEnabled):
                case nameof(MenuItemData.IsExpanded):
                case nameof(MenuItemData.IsInteractive):
                    this.UpdateCommonVisualState();
                    break;

                case nameof(MenuItemData.Icon):
                    this.UpdateIconVisualState();
                    break;

                case nameof(MenuItemData.AcceleratorText):
                    this.UpdateAcceleratorVisualState();
                    break;

                case nameof(MenuItemData.Mnemonic):
                    this.AccessKey = this.ItemData?.Mnemonic?.ToString() ?? string.Empty;
                    this.RefreshTextPresentation();
                    break;

                case nameof(MenuItemData.Text):
                    this.RefreshTextPresentation();
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
                    // For other properties (Command, etc.), no visual state update needed
                    // The control simply responds to specific property changes above
                    break;
            }
        });
    }
}

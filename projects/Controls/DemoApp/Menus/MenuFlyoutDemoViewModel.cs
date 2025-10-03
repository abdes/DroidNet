// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuFlyout presentation using MenuBuilder.
/// Shows context menus with proper UX patterns.
/// </summary>
public partial class MenuFlyoutDemoViewModel : ObservableObject
{
    [ObservableProperty]
    public partial string LastActionMessage { get; set; } = "Right-click the areas below to see context menus.";

    [ObservableProperty]
    public partial bool WordWrapEnabled { get; set; } = false;

    [ObservableProperty]
    public partial string TextAlignment { get; set; } = "Left";

    [ObservableProperty]
    public partial MenuFlyout? ContextMenuFlyout { get; set; }

    // Store references to menu items for radio group handling
    private MenuItemData? alignLeftItem;
    private MenuItemData? alignCenterItem;
    private MenuItemData? alignRightItem;

    public MenuFlyoutDemoViewModel()
    {
        this.BuildContextMenu();
    }

    /// <summary>
    /// Builds a context menu demonstration.
    /// </summary>
    private void BuildContextMenu()
    {
        var menuBuilder = new MenuBuilder()
            .AddMenuItem(new MenuItemData
            {
                Text = "Context Cut",
                Icon = new SymbolIconSource { Symbol = Symbol.Cut },
                AcceleratorText = "Ctrl+X",
                Command = this.CreateCommand("Text cut to clipboard"),
            })
            .AddMenuItem(new MenuItemData
            {
                Text = "Context Copy",
                Icon = new SymbolIconSource { Symbol = Symbol.Copy },
                AcceleratorText = "Ctrl+C",
                Command = this.CreateCommand("Text copied to clipboard"),
            })
            .AddMenuItem(new MenuItemData
            {
                Text = "Context Paste",
                Icon = new SymbolIconSource { Symbol = Symbol.Paste },
                AcceleratorText = "Ctrl+V",
                Command = this.CreateCommand("Text pasted from clipboard"),
            })
            .AddMenuItem(new MenuItemData { IsSeparator = true })
            .AddMenuItem(new MenuItemData
            {
                Text = "Context Format",
                SubItems = new[]
                {
                    new MenuItemData
                    {
                        Text = "Context Word Wrap",
                        IsCheckable = true,
                        IsChecked = this.WordWrapEnabled,
                        Command = new RelayCommand(() =>
                        {
                            this.WordWrapEnabled = !this.WordWrapEnabled;
                            this.LastActionMessage = $"Word Wrap: {(this.WordWrapEnabled ? "ON" : "OFF")}";
                        }),
                    },
                    new MenuItemData { IsSeparator = true },
                    this.alignLeftItem = new MenuItemData
                    {
                        Text = "Context Align Left",
                        RadioGroupId = "Alignment",
                        IsChecked = string.Equals(this.TextAlignment, "Left", StringComparison.Ordinal),
                        Command = new RelayCommand(() => this.SelectAlignment("Left")),
                    },
                    this.alignCenterItem = new MenuItemData
                    {
                        Text = "Context Align Center",
                        RadioGroupId = "Alignment",
                        IsChecked = string.Equals(this.TextAlignment, "Center", StringComparison.Ordinal),
                        Command = new RelayCommand(() => this.SelectAlignment("Center")),
                    },
                    this.alignRightItem = new MenuItemData
                    {
                        Text = "Context Align Right",
                        RadioGroupId = "Alignment",
                        IsChecked = string.Equals(this.TextAlignment, "Right", StringComparison.Ordinal),
                        Command = new RelayCommand(() => this.SelectAlignment("Right")),
                    },
                },
            });

        this.ContextMenuFlyout = menuBuilder.BuildMenuFlyout();
    }

    /// <summary>
    /// Handles alignment selection with proper radio group behavior.
    /// </summary>
    /// <param name="alignment">The alignment to select.</param>
    private void SelectAlignment(string alignment)
    {
        // Implement radio group behavior - deselect all items in the alignment group
        this.alignLeftItem?.IsChecked = false;

        this.alignCenterItem?.IsChecked = false;

        this.alignRightItem?.IsChecked = false;

        // Select the clicked item
        var selectedItem = alignment switch
        {
            "Left" => this.alignLeftItem,
            "Center" => this.alignCenterItem,
            "Right" => this.alignRightItem,
            _ => null,
        };

        selectedItem?.IsChecked = true;

        this.TextAlignment = alignment;
        this.LastActionMessage = $"Text aligned {alignment.ToUpperInvariant()}";
    }

    private RelayCommand CreateCommand(string message)
    {
        return new RelayCommand(() => this.LastActionMessage = message);
    }
}

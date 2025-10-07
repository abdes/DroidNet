// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuFlyout presentation using MenuBuilder.
/// Shows context menus with proper UX patterns.
/// </summary>
public partial class MenuFlyoutDemoViewModel : ObservableObject
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MenuFlyoutDemoViewModel"/> class.
    /// </summary>
    public MenuFlyoutDemoViewModel()
    {
        this.ContextMenu = this.BuildContextMenu();
        this.SimpleMenu = this.BuildSimpleMenu();
    }

    /// <summary>
    /// Gets the menu source leveraged by the dynamic context flyout demo.
    /// </summary>
    public IMenuSource ContextMenu { get; }

    /// <summary>
    /// Gets the simple menu source that powers the static context flyout demo.
    /// </summary>
    public IMenuSource SimpleMenu { get; }

    [ObservableProperty]
    public partial string LastActionMessage { get; set; } = "Right-click the areas below to see context menus.";

    [ObservableProperty]
    public partial bool WordWrapEnabled { get; set; } = false;

    [ObservableProperty]
    public partial string TextAlignment { get; set; } = "Left";

    private IMenuSource BuildContextMenu()
    {
        var menuBuilder = new MenuBuilder()
            .AddMenuItem(this.CreateClipboardItem("Context Cut", Symbol.Cut, "Ctrl+X", "Text cut to clipboard"))
            .AddMenuItem(this.CreateClipboardItem("Context Copy", Symbol.Copy, "Ctrl+C", "Text copied to clipboard"))
            .AddMenuItem(this.CreateClipboardItem("Context Paste", Symbol.Paste, "Ctrl+V", "Text pasted from clipboard"))
            .AddSeparator()
            .AddMenuItem(this.CreateFormattingSubmenu());

        return menuBuilder.Build();
    }

    private IMenuSource BuildSimpleMenu()
    {
        var menuBuilder = new MenuBuilder()
            .AddMenuItem(new MenuItemData { Text = "📝 Edit", Command = this.CreateCommand("Edit action invoked") })
            .AddMenuItem(new MenuItemData { Text = "📋 Copy", Command = this.CreateCommand("Copy action invoked") })
            .AddMenuItem(new MenuItemData { Text = "🗑️ Delete", Command = this.CreateCommand("Delete action invoked") })
            .AddSeparator()
            .AddMenuItem(new MenuItemData { Text = "📋 Properties", Command = this.CreateCommand("Properties opened") });

        return menuBuilder.Build();
    }

    private MenuItemData CreateClipboardItem(string text, Symbol symbol, string accelerator, string message)
    {
        return new MenuItemData
        {
            Text = text,
            Icon = new SymbolIconSource { Symbol = symbol },
            AcceleratorText = accelerator,
            Command = this.CreateCommand(message),
        };
    }

    private MenuItemData CreateFormattingSubmenu()
    {
        return new MenuItemData
        {
            Text = "Context Format",
            SubItems =
            [
                this.CreateWordWrapItem(),
                new MenuItemData { IsSeparator = true },
                this.CreateAlignmentItem("Left"),
                this.CreateAlignmentItem("Center"),
                this.CreateAlignmentItem("Right"),
            ],
        };
    }

    private MenuItemData CreateWordWrapItem()
    {
        return new MenuItemData
        {
            Text = "Context Word Wrap",
            IsCheckable = true,
            IsChecked = this.WordWrapEnabled,
            Command = new RelayCommand<MenuItemData?>(this.ToggleWordWrap),
        };
    }

    private MenuItemData CreateAlignmentItem(string alignment)
    {
        return new MenuItemData
        {
            Text = alignment switch
            {
                "Center" => "Context Align Center",
                "Right" => "Context Align Right",
                _ => "Context Align Left",
            },
            RadioGroupId = "Alignment",
            IsChecked = string.Equals(this.TextAlignment, alignment, StringComparison.Ordinal),
            Command = new RelayCommand<MenuItemData?>(this.SelectAlignment),
        };
    }

    private void ToggleWordWrap(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        this.WordWrapEnabled = menuItem.IsChecked;
        this.LastActionMessage = $"Word Wrap: {(menuItem.IsChecked ? "ON" : "OFF")}";
    }

    private void SelectAlignment(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        var alignment = menuItem.Text.Contains("Center", StringComparison.OrdinalIgnoreCase)
            ? "Center"
            : menuItem.Text.Contains("Right", StringComparison.OrdinalIgnoreCase)
                ? "Right"
                : "Left";

        this.TextAlignment = alignment;
        this.LastActionMessage = $"Text aligned {alignment.ToUpperInvariant()}";
    }

    private RelayCommand CreateCommand(string message)
    {
        return new RelayCommand(() => this.LastActionMessage = message);
    }
}

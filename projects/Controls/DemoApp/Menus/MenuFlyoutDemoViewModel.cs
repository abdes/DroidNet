// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuFlyout in two scenarios using a single menu specification.
/// Shows the same menu activated via context menu (right-click) and MenuButton (click).
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModels must be public")]
public partial class MenuFlyoutDemoViewModel : ObservableObject
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MenuFlyoutDemoViewModel"/> class.
    /// </summary>
    /// <param name="loggerFactory">The factory used to create loggers for menu operations.</param>
    public MenuFlyoutDemoViewModel(ILoggerFactory loggerFactory)
    {
        this.SharedMenu = this.BuildSharedMenu(loggerFactory);
    }

    /// <summary>
    /// Gets the shared menu source used by both the context menu and MenuButton scenarios.
    /// </summary>
    public IMenuSource SharedMenu { get; }

    [ObservableProperty]
    public partial string LastActionMessage { get; set; } = "Right-click the left area or click the button on the right.";

    [ObservableProperty]
    public partial bool WordWrapEnabled { get; set; } = false;

    [ObservableProperty]
    public partial string TextAlignment { get; set; } = "Left";

    private IMenuSource BuildSharedMenu(ILoggerFactory loggerFactory)
    {
        var menuBuilder = new MenuBuilder(loggerFactory)
            .AddMenuItem(this.CreateClipboardItem("Cut", Symbol.Cut, "Ctrl+X", "Text cut to clipboard"))
            .AddMenuItem(this.CreateClipboardItem("Copy", Symbol.Copy, "Ctrl+C", "Text copied to clipboard"))
            .AddMenuItem(this.CreateClipboardItem("Paste", Symbol.Paste, "Ctrl+V", "Text pasted from clipboard"))
            .AddSeparator()
            .AddMenuItem(this.CreateFormattingSubmenu());

        return menuBuilder.Build();
    }

    private MenuItemData CreateClipboardItem(string text, Symbol symbol, string accelerator, string message)
        => new()
        {
            Text = text,
            Icon = new FontIconSource
            {
                FontFamily = new FontFamily("Segoe MDL2 Assets"),
                Glyph = ((char)symbol).ToString(),
                FontSize = 16,
            },
            AcceleratorText = accelerator,
            Command = this.CreateCommand(message),
        };

    private MenuItemData CreateFormattingSubmenu()
        => new()
        {
            Text = "Format",
            SubItems =
            [
                this.CreateWordWrapItem(),
                new MenuItemData { IsSeparator = true },
                this.CreateAlignmentItem("Left"),
                this.CreateAlignmentItem("Center"),
                this.CreateAlignmentItem("Right"),
            ],
        };

    private MenuItemData CreateWordWrapItem()
        => new()
        {
            Text = "Word Wrap",
            IsCheckable = true,
            IsChecked = this.WordWrapEnabled,
            Command = new RelayCommand<MenuItemData?>(this.ToggleWordWrap),
        };

    private MenuItemData CreateAlignmentItem(string alignment)
        => new()
        {
            Text = alignment switch
            {
                "Center" => "Align Center",
                "Right" => "Align Right",
                _ => "Align Left",
            },
            RadioGroupId = "Alignment",
            IsChecked = string.Equals(this.TextAlignment, alignment, StringComparison.Ordinal),
            Command = new RelayCommand<MenuItemData?>(this.SelectAlignment),
        };

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
        => new(() => this.LastActionMessage = message);
}

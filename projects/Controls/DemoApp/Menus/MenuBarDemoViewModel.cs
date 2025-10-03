// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuBar presentation using MenuBuilder.
/// Shows how to build a traditional WinUI MenuBar with proper UX patterns.
/// </summary>
public partial class MenuBarDemoViewModel : ObservableObject
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MenuBarDemoViewModel"/> class.
    /// </summary>
    public MenuBarDemoViewModel()
    {
        this.MenuBarSource = this.BuildMenuBar();
    }

    /// <summary>
    /// Gets the menu source driving the demo menu bar.
    /// </summary>
    public IMenuSource MenuBarSource { get; }

    [ObservableProperty]
    public partial string LastActionMessage { get; set; } = "Welcome! Try the MenuBar items above.";

    [ObservableProperty]
    public partial bool AutoSaveEnabled { get; set; } = true;

    [ObservableProperty]
    public partial string SelectedTheme { get; set; } = "Light";

    /// <summary>
    /// Builds a MenuBar demonstration showing proper UX patterns.
    /// </summary>
    private IMenuSource BuildMenuBar()
    {
        var menuBuilder = new MenuBuilder()
            .AddMenuItem(new MenuItemData
            {
                Text = "MenuBar File",
                SubItems = new[]
                {
                    this.CreateMenuCommand("MenuBar New Document", Symbol.Add, "Ctrl+N", "New document created!"),
                    this.CreateMenuCommand("MenuBar Open", Symbol.OpenFile, "Ctrl+O", "File opened!"),
                    new MenuItemData { IsSeparator = true },
                    new MenuItemData
                    {
                        Text = "MenuBar Auto Save",
                        Icon = new SymbolIconSource { Symbol = Symbol.Save },
                        IsCheckable = true,
                        IsChecked = this.AutoSaveEnabled,
                        Command = new RelayCommand(() =>
                        {
                            this.AutoSaveEnabled = !this.AutoSaveEnabled;
                            this.LastActionMessage = $"Auto Save: {(this.AutoSaveEnabled ? "ON" : "OFF")}";
                        }),
                    },
                },
            })
            .AddMenuItem(new MenuItemData
            {
                Text = "MenuBar View",
                SubItems = new[]
                {
                    this.CreateThemeMenuItem("MenuBar Light Theme", "Light", Symbol.Clear),
                    this.CreateThemeMenuItem("MenuBar Dark Theme", "Dark", Symbol.Globe),
                },
            });

        return menuBuilder.Build();
    }

    /// <summary>
    /// Handles theme selection with proper radio group behavior.
    /// </summary>
    /// <param name="theme">The theme to select.</param>
    private void SelectTheme(string theme)
    {
        this.SelectedTheme = theme;
        this.LastActionMessage = $"{theme} theme selected";
    }

    private MenuItemData CreateMenuCommand(string text, Symbol icon, string accelerator, string message)
    {
        return new MenuItemData
        {
            Text = text,
            Icon = new SymbolIconSource { Symbol = icon },
            AcceleratorText = accelerator,
            Command = this.CreateCommand(message),
        };
    }

    private MenuItemData CreateThemeMenuItem(string text, string theme, Symbol icon)
    {
        return new MenuItemData
        {
            Text = text,
            Icon = new SymbolIconSource { Symbol = icon },
            RadioGroupId = "Theme",
            IsChecked = string.Equals(this.SelectedTheme, theme, StringComparison.Ordinal),
            Command = new RelayCommand(() => this.SelectTheme(theme)),
        };
    }

    private RelayCommand CreateCommand(string message)
    {
        return new RelayCommand(() => this.LastActionMessage = message);
    }
}

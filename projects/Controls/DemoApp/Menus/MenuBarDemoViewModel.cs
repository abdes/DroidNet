// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuBar presentation using MenuBuilder.
/// Shows how to build a traditional WinUI MenuBar with proper UX patterns.
/// </summary>
public partial class MenuBarDemoViewModel : ObservableObject
{
    [ObservableProperty]
    public partial string LastActionMessage { get; set; } = "Welcome! Try the MenuBar items above.";

    [ObservableProperty]
    public partial bool AutoSaveEnabled { get; set; } = true;

    [ObservableProperty]
    public partial string SelectedTheme { get; set; } = "Light";

    [ObservableProperty]
    public partial MenuBar? MenuBar { get; set; }

    // Store references to menu items for radio group handling
    private MenuItemData? viewSubmenu;
    private MenuItemData? lightThemeItem;
    private MenuItemData? darkThemeItem;
    private MenuItemData? autoSaveItem;

    public MenuBarDemoViewModel()
    {
        this.BuildMenuBar();
    }

    /// <summary>
    /// Builds a MenuBar demonstration showing proper UX patterns.
    /// </summary>
    private void BuildMenuBar()
    {
        var menuBuilder = new MenuBuilder()
            .AddMenuItem(new MenuItemData
            {
                Text = "MenuBar File",
                SubItems = new[]
                {
                    new MenuItemData
                    {
                        Text = "MenuBar New Document",
                        Icon = new SymbolIconSource { Symbol = Symbol.Add },
                        AcceleratorText = "Ctrl+N",
                        Command = this.CreateCommand("New document created!"),
                    },
                    new MenuItemData
                    {
                        Text = "MenuBar Open",
                        Icon = new SymbolIconSource { Symbol = Symbol.OpenFile },
                        AcceleratorText = "Ctrl+O",
                        Command = this.CreateCommand("File opened!"),
                    },
                    new MenuItemData { IsSeparator = true },
                    this.autoSaveItem = new MenuItemData
                    {
                        Text = "MenuBar Auto Save",
                        Icon = new SymbolIconSource { Symbol = Symbol.Save },
                        IsCheckable = true,
                        IsChecked = this.AutoSaveEnabled,
                        Command = new RelayCommand(() =>
                        {
                            this.AutoSaveEnabled = !this.AutoSaveEnabled;
                            this.autoSaveItem?.IsChecked = this.AutoSaveEnabled;
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
                    // Store reference for radio group handling
                    this.lightThemeItem = new MenuItemData
                    {
                        Text = "MenuBar Light Theme",
                        Icon = new SymbolIconSource { Symbol = Symbol.Clear },
                        RadioGroupId = "Theme",
                        IsChecked = string.Equals(this.SelectedTheme, "Light", StringComparison.Ordinal),
                        Command = new RelayCommand(() => this.SelectTheme("Light")),
                    },
                    this.darkThemeItem = new MenuItemData
                    {
                        Text = "MenuBar Dark Theme",
                        Icon = new SymbolIconSource { Symbol = Symbol.Globe },
                        RadioGroupId = "Theme",
                        IsChecked = string.Equals(this.SelectedTheme, "Dark", StringComparison.Ordinal),
                        Command = new RelayCommand(() => this.SelectTheme("Dark")),
                    },
                },
            });

        this.MenuBar = menuBuilder.BuildMenuBar();
    }

    /// <summary>
    /// Handles theme selection with proper radio group behavior.
    /// </summary>
    /// <param name="theme">The theme to select.</param>
    private void SelectTheme(string theme)
    {
        // Implement radio group behavior - deselect all items in the theme group
        this.lightThemeItem?.IsChecked = false;

        this.darkThemeItem?.IsChecked = false;

        // Select the clicked item
        var selectedItem = theme switch
        {
            "Light" => this.lightThemeItem,
            "Dark" => this.darkThemeItem,
            _ => null,
        };

        selectedItem?.IsChecked = true;

        this.SelectedTheme = theme;
        this.LastActionMessage = $"{theme} theme selected";
    }

    private RelayCommand CreateCommand(string message)
    {
        return new RelayCommand(() => this.LastActionMessage = message);
    }
}

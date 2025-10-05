// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

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
    public partial bool ShowStatusBar { get; set; } = true;

    [ObservableProperty]
    public partial bool ShowLineNumbers { get; set; } = true;

    [ObservableProperty]
    public partial bool ShowMinimap { get; set; } = true;

    [ObservableProperty]
    public partial bool WordWrapEnabled { get; set; } = false;

    [ObservableProperty]
    public partial string SelectedTheme { get; set; } = "Light";

    /// <summary>
    /// Builds a MenuBar demonstration showing proper UX patterns.
    /// </summary>
    private IMenuSource BuildMenuBar()
    {
        var menuBuilder = new MenuBuilder()
            .AddMenuItem(this.CreateFileMenu())
            .AddMenuItem(this.CreateEditMenu())
            .AddMenuItem(this.CreateViewMenu())
            .AddMenuItem(this.CreateNavigateMenu())
            .AddMenuItem(this.CreateRunMenu())
            .AddMenuItem(this.CreateToolsMenu())
            .AddMenuItem(this.CreateHelpMenu());

        return menuBuilder.Build();
    }

    private MenuItemData CreateFileMenu() => new()
    {
        Text = "File",
        Mnemonic = 'F',
        SubItems =
            [
                new MenuItemData
                {
                    Text = "New",
                    Icon = IconHelper.Create(Symbol.Add),
                    Mnemonic = 'N',
                    SubItems =
                    [
                        this.CreateMenuCommand("New File", Symbol.Page, 'F', "Ctrl+N", "Created a new file"),
                        this.CreateMenuCommand("New Window", Symbol.OpenFile, 'W', "Ctrl+Shift+N", "Opened a new window"),
                        this.CreateMenuCommand("New Project...", Symbol.Folder, 'P', "Ctrl+Shift+P", "Started the new project wizard"),
                    ],
                },
                new MenuItemData
                {
                    Text = "Open",
                    Icon = IconHelper.Create(Symbol.OpenFile),
                    Mnemonic = 'O',
                    SubItems =
                    [
                        this.CreateMenuCommand("Open File...", Symbol.OpenFile, 'F', "Ctrl+O", "Opened a file"),
                        this.CreateMenuCommand("Open Folder...", Symbol.Folder, 'L', "Ctrl+K, Ctrl+O", "Opened a folder"),
                        this.CreateMenuCommand("Open Workspace...", Symbol.Folder, 'W', "Ctrl+Shift+W", "Opened a workspace"),
                        new MenuItemData { IsSeparator = true },
                        this.CreateMenuCommand("Recent Workspace: DroidNet", Symbol.Clock, 'R', null, "Reopened workspace 'DroidNet'"),
                        this.CreateMenuCommand("Recent File: MenuBarDemoView.xaml", Symbol.Document, 'F', null, "Reopened recent file 'MenuBarDemoView.xaml'"),
                    ],
                },
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Save", Symbol.Save, 'S', "Ctrl+S", "Saved the active document"),
                this.CreateMenuCommand("Save As...", Symbol.SaveLocal, 'A', "Ctrl+Shift+S", "Opened Save As dialog"),
                this.CreateMenuCommand("Save All", Symbol.Save, 'L', "Ctrl+Alt+S", "Saved all open documents"),
                this.CreateMenuCommand("Revert File", Symbol.Refresh, 'R', null, "Reverted changes in the current file"),
                new MenuItemData { IsSeparator = true },
                this.CreateToggleMenuItem("Auto Save", Symbol.Save, 'U', () => this.AutoSaveEnabled, value => this.AutoSaveEnabled = value),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Preferences...", Symbol.Setting, 'P', "Ctrl+,", "Opened preferences"),
                this.CreateMenuCommand("Exit", Symbol.LeaveChat, 'E', null, "Exited the editor"),
            ],
    };

    private MenuItemData CreateEditMenu() => new()
    {
        Text = "Edit",
        Mnemonic = 'E',
        SubItems =
            [
                this.CreateMenuCommand("Undo", Symbol.Undo, 'U', "Ctrl+Z", "Undo performed"),
                this.CreateMenuCommand("Redo", Symbol.Redo, 'R', "Ctrl+Y", "Redo performed"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Cut", Symbol.Cut, 'C', "Ctrl+X", "Cut selection"),
                this.CreateMenuCommand("Copy", Symbol.Copy, 'O', "Ctrl+C", "Copied selection"),
                this.CreateMenuCommand("Paste", Symbol.Paste, 'P', "Ctrl+V", "Pasted clipboard contents"),
                this.CreateMenuCommand("Delete", Symbol.Delete, 'D', "Del", "Deleted selection"),
                this.CreateMenuCommand("Select All", Symbol.SelectAll, 'A', "Ctrl+A", "Selected all content"),
                new MenuItemData { IsSeparator = true },
                new MenuItemData
                {
                    Text = "Find",
                    Icon = IconHelper.Create(Symbol.Find),
                    Mnemonic = 'F',
                    SubItems =
                    [
                        this.CreateMenuCommand("Find...", Symbol.Find, 'F', "Ctrl+F", "Opened find dialog"),
                        this.CreateMenuCommand("Replace...", Symbol.Find, 'R', "Ctrl+H", "Opened replace dialog"),
                        this.CreateMenuCommand("Find in Files...", Symbol.Find, 'I', "Ctrl+Shift+F", "Opened find in files"),
                    ],
                },
                new MenuItemData
                {
                    Text = "Go To",
                    Icon = IconHelper.Create(Symbol.GoToStart),
                    Mnemonic = 'G',
                    SubItems =
                    [
                        this.CreateMenuCommand("Go to Line...", Symbol.GoToStart, 'L', "Ctrl+G", "Navigated to line"),
                        this.CreateMenuCommand("Go to Symbol...", Symbol.Import, 'S', "Ctrl+Shift+O", "Opened symbol navigator"),
                        this.CreateMenuCommand("Go to File...", Symbol.OpenFile, 'F', "Ctrl+P", "Opened quick file navigation"),
                    ],
                },
            ],
    };

    private MenuItemData CreateViewMenu() => new()
    {
        Text = "View",
        Mnemonic = 'V',
        SubItems =
            [
                new MenuItemData
                {
                    Text = "Appearance",
                    Icon = IconHelper.Create(Symbol.View),
                    Mnemonic = 'A',
                    SubItems =
                    [
                        this.CreateToggleMenuItem("Show Status Bar", Symbol.View, 'S', () => this.ShowStatusBar, value => this.ShowStatusBar = value),
                        this.CreateToggleMenuItem("Show Minimap", Symbol.Map, 'M', () => this.ShowMinimap, value => this.ShowMinimap = value),
                        this.CreateToggleMenuItem("Show Line Numbers", Symbol.Bullets, 'L', () => this.ShowLineNumbers, value => this.ShowLineNumbers = value),
                        this.CreateToggleMenuItem("Word Wrap", Symbol.Switch, 'W', () => this.WordWrapEnabled, value => this.WordWrapEnabled = value),
                    ],
                },
                new MenuItemData
                {
                    Text = "Zoom",
                    Icon = IconHelper.Create(Symbol.Zoom),
                    Mnemonic = 'Z',
                    SubItems =
                    [
                        this.CreateMenuCommand("Zoom In", Symbol.ZoomIn, 'I', "Ctrl+=", "Zoomed in"),
                        this.CreateMenuCommand("Zoom Out", Symbol.ZoomOut, 'O', "Ctrl+-", "Zoomed out"),
                        this.CreateMenuCommand("Reset Zoom", Symbol.Zoom, 'R', "Ctrl+0", "Reset zoom level"),
                    ],
                },
                new MenuItemData { IsSeparator = true },
                new MenuItemData
                {
                    Text = "Theme",
                    Icon = IconHelper.Create(Symbol.SolidStar),
                    Mnemonic = 'T',
                    SubItems =
                    [
                        this.CreateThemeMenuItem("Light", "Light", Symbol.SolidStar, 'L'),
                        this.CreateThemeMenuItem("Dark", "Dark", Symbol.OutlineStar, 'D'),
                        this.CreateThemeMenuItem("High Contrast", "High Contrast", Symbol.Important, 'H'),
                    ],
                },
            ],
    };

    private MenuItemData CreateNavigateMenu() => new()
    {
        Text = "Navigate",
        Mnemonic = 'N',
        SubItems =
            [
                this.CreateMenuCommand("Go Back", Symbol.Back, 'B', "Alt+Left", "Navigated back"),
                this.CreateMenuCommand("Go Forward", Symbol.Forward, 'F', "Alt+Right", "Navigated forward"),
                this.CreateMenuCommand("Go to Definition", Symbol.Target, 'D', "F12", "Navigated to definition"),
                this.CreateMenuCommand("Go to Declaration", Symbol.Target, 'E', "Ctrl+F12", "Navigated to declaration"),
                this.CreateMenuCommand("Peek Definition", Symbol.Preview, 'P', "Alt+F12", "Opened peek definition"),
                this.CreateMenuCommand("Toggle Editor/Terminal", Symbol.Switch, 'T', "Ctrl+`", "Toggled editor and terminal focus"),
                this.CreateMenuCommand("Navigate to Matching Brace", Symbol.GoToStart, 'M', "Ctrl+]", "Jumped to matching brace"),
            ],
    };

    private MenuItemData CreateRunMenu() => new()
    {
        Text = "Run",
        Mnemonic = 'R',
        SubItems =
            [
                this.CreateMenuCommand("Start Debugging", Symbol.Play, 'S', "F5", "Started debugging"),
                this.CreateMenuCommand("Run Without Debugging", Symbol.Play, 'W', "Ctrl+F5", "Started without debugging"),
                this.CreateMenuCommand("Stop Debugging", Symbol.Stop, 'T', "Shift+F5", "Stopped debugging"),
                this.CreateMenuCommand("Restart Debugging", Symbol.Refresh, 'R', "Ctrl+Shift+F5", "Restarted debugging"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Toggle Breakpoint", Symbol.Flag, 'B', "F9", "Toggled breakpoint"),
                this.CreateMenuCommand("Step Over", Symbol.Forward, 'O', "F10", "Stepped over"),
                this.CreateMenuCommand("Step Into", Symbol.Forward, 'I', "F11", "Stepped into"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Open Run Configurations...", Symbol.Setting, 'C', null, "Opened run configurations"),
            ],
    };

    private MenuItemData CreateToolsMenu() => new()
    {
        Text = "Tools",
        Mnemonic = 'T',
        SubItems =
            [
                this.CreateMenuCommand("Command Palette...", Symbol.Switch, 'C', "Ctrl+Shift+P", "Opened command palette"),
                this.CreateMenuCommand("Extensions...", Symbol.Manage, 'E', "Ctrl+Shift+X", "Opened extensions manager"),
                this.CreateMenuCommand("Run Task...", Symbol.Sync, 'R', "Ctrl+Shift+B", "Opened task runner"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Toggle Terminal", Symbol.Contact, 'T', "Ctrl+`", "Toggled integrated terminal"),
                this.CreateMenuCommand("Open Settings (JSON)", Symbol.Setting, 'S', "Ctrl+Shift+,", "Opened settings (JSON)"),
            ],
    };

    private MenuItemData CreateHelpMenu() => new()
    {
        Text = "Help",
        Mnemonic = 'H',
        SubItems =
            [
                this.CreateMenuCommand("View Documentation", Symbol.Help, 'V', "F1", "Opened documentation"),
                this.CreateMenuCommand("Release Notes", Symbol.Read, 'R', null, "Opened release notes"),
                this.CreateMenuCommand("Report Issue...", Symbol.Flag, 'I', null, "Opened issue reporter"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("About DroidNet Editor", Symbol.Contact, 'A', null, "Opened About dialog"),
            ],
    };

    /// <summary>
    /// Handles theme selection with proper radio group behavior.
    /// </summary>
    /// <param name="theme">The theme to select.</param>
    private void SelectTheme(string theme)
    {
        this.SelectedTheme = theme;
        this.LastActionMessage = $"{theme} theme selected";
    }

    private MenuItemData CreateToggleMenuItem(string text, Symbol icon, char mnemonic, Func<bool> getState, Action<bool> setState)
    {
        var item = new MenuItemData
        {
            Text = text,
            Icon = IconHelper.Create(icon),
            Mnemonic = mnemonic,
            IsCheckable = true,
            IsChecked = getState(),
        };

        item.Command = new RelayCommand(() =>
        {
            var newValue = !getState();
            setState(newValue);
            item.IsChecked = newValue;
            this.LastActionMessage = $"{text}: {(newValue ? "ON" : "OFF")}";
        });

        return item;
    }

    private MenuItemData CreateMenuCommand(string text, Symbol icon, char mnemonic, string? accelerator, string message) => new()
    {
        Text = text,
        Icon = IconHelper.Create(icon),
        Mnemonic = mnemonic,
        AcceleratorText = accelerator,
        Command = this.CreateCommand(message),
    };

    private MenuItemData CreateThemeMenuItem(string text, string theme, Symbol icon, char mnemonic) => new()
    {
        Text = text,
        Icon = IconHelper.Create(icon),
        Mnemonic = mnemonic,
        RadioGroupId = "Theme",
        IsChecked = string.Equals(this.SelectedTheme, theme, StringComparison.Ordinal),
        Command = new RelayCommand(() => this.SelectTheme(theme)),
    };

    private RelayCommand CreateCommand(string message) => new(() => this.LastActionMessage = message);

    private static class IconHelper
    {
        private static readonly FontFamily SymbolFontFamily = new("Segoe Fluent Icons");

        public static FontIconSource Create(Symbol symbol) => new()
        {
            Glyph = ((char)symbol).ToString(),
            FontFamily = SymbolFontFamily,
            FontSize = 16,
            IsTextScaleFactorEnabled = false,
        };
    }
}

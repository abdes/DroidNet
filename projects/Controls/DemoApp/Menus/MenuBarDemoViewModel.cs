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
        SubItems =
            [
                new MenuItemData
                {
                    Text = "New",
                    Icon = IconHelper.Create(Symbol.Add),
                    SubItems =
                    [
                        this.CreateMenuCommand("New File", Symbol.Page, "Ctrl+N", "Created a new file"),
                        this.CreateMenuCommand("New Window", Symbol.OpenFile, "Ctrl+Shift+N", "Opened a new window"),
                        this.CreateMenuCommand("New Project...", Symbol.Folder, "Ctrl+Shift+P", "Started the new project wizard"),
                    ],
                },
                new MenuItemData
                {
                    Text = "Open",
                    Icon = IconHelper.Create(Symbol.OpenFile),
                    SubItems =
                    [
                        this.CreateMenuCommand("Open File...", Symbol.OpenFile, "Ctrl+O", "Opened a file"),
                        this.CreateMenuCommand("Open Folder...", Symbol.Folder, "Ctrl+K, Ctrl+O", "Opened a folder"),
                        this.CreateMenuCommand("Open Workspace...", Symbol.Folder, "Ctrl+Shift+W", "Opened a workspace"),
                        new MenuItemData { IsSeparator = true },
                        this.CreateMenuCommand("Recent Workspace: DroidNet", Symbol.Clock, null, "Reopened workspace 'DroidNet'"),
                        this.CreateMenuCommand("Recent File: MenuBarDemoView.xaml", Symbol.Document, null, "Reopened recent file 'MenuBarDemoView.xaml'"),
                    ],
                },
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Save", Symbol.Save, "Ctrl+S", "Saved the active document"),
                this.CreateMenuCommand("Save As...", Symbol.SaveLocal, "Ctrl+Shift+S", "Opened Save As dialog"),
                this.CreateMenuCommand("Save All", Symbol.Save, "Ctrl+Alt+S", "Saved all open documents"),
                this.CreateMenuCommand("Revert File", Symbol.Refresh, null, "Reverted changes in the current file"),
                new MenuItemData { IsSeparator = true },
                this.CreateToggleMenuItem("Auto Save", Symbol.Save, () => this.AutoSaveEnabled, value => this.AutoSaveEnabled = value),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Preferences...", Symbol.Setting, "Ctrl+,", "Opened preferences"),
                this.CreateMenuCommand("Exit", Symbol.LeaveChat, null, "Exited the editor"),
            ],
    };

    private MenuItemData CreateEditMenu() => new()
    {
        Text = "Edit",
        SubItems =
            [
                this.CreateMenuCommand("Undo", Symbol.Undo, "Ctrl+Z", "Undo performed"),
                this.CreateMenuCommand("Redo", Symbol.Redo, "Ctrl+Y", "Redo performed"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Cut", Symbol.Cut, "Ctrl+X", "Cut selection"),
                this.CreateMenuCommand("Copy", Symbol.Copy, "Ctrl+C", "Copied selection"),
                this.CreateMenuCommand("Paste", Symbol.Paste, "Ctrl+V", "Pasted clipboard contents"),
                this.CreateMenuCommand("Delete", Symbol.Delete, "Del", "Deleted selection"),
                this.CreateMenuCommand("Select All", Symbol.SelectAll, "Ctrl+A", "Selected all content"),
                new MenuItemData { IsSeparator = true },
                new MenuItemData
                {
                    Text = "Find",
                    Icon = IconHelper.Create(Symbol.Find),
                    SubItems =
                    [
                        this.CreateMenuCommand("Find...", Symbol.Find, "Ctrl+F", "Opened find dialog"),
                        this.CreateMenuCommand("Replace...", Symbol.Find, "Ctrl+H", "Opened replace dialog"),
                        this.CreateMenuCommand("Find in Files...", Symbol.Find, "Ctrl+Shift+F", "Opened find in files"),
                    ],
                },
                new MenuItemData
                {
                    Text = "Go To",
                    Icon = IconHelper.Create(Symbol.GoToStart),
                    SubItems =
                    [
                        this.CreateMenuCommand("Go to Line...", Symbol.GoToStart, "Ctrl+G", "Navigated to line"),
                        this.CreateMenuCommand("Go to Symbol...", Symbol.Import, "Ctrl+Shift+O", "Opened symbol navigator"),
                        this.CreateMenuCommand("Go to File...", Symbol.OpenFile, "Ctrl+P", "Opened quick file navigation"),
                    ],
                },
            ],
    };

    private MenuItemData CreateViewMenu() => new()
    {
        Text = "View",
        SubItems =
            [
                new MenuItemData
                {
                    Text = "Appearance",
                    Icon = IconHelper.Create(Symbol.View),
                    SubItems =
                    [
                        this.CreateToggleMenuItem("Show Status Bar", Symbol.View, () => this.ShowStatusBar, value => this.ShowStatusBar = value),
                        this.CreateToggleMenuItem("Show Minimap", Symbol.Map, () => this.ShowMinimap, value => this.ShowMinimap = value),
                        this.CreateToggleMenuItem("Show Line Numbers", Symbol.Bullets, () => this.ShowLineNumbers, value => this.ShowLineNumbers = value),
                        this.CreateToggleMenuItem("Word Wrap", Symbol.Switch, () => this.WordWrapEnabled, value => this.WordWrapEnabled = value),
                    ],
                },
                new MenuItemData
                {
                    Text = "Zoom",
                    Icon = IconHelper.Create(Symbol.Zoom),
                    SubItems =
                    [
                        this.CreateMenuCommand("Zoom In", Symbol.ZoomIn, "Ctrl+=", "Zoomed in"),
                        this.CreateMenuCommand("Zoom Out", Symbol.ZoomOut, "Ctrl+-", "Zoomed out"),
                        this.CreateMenuCommand("Reset Zoom", Symbol.Zoom, "Ctrl+0", "Reset zoom level"),
                    ],
                },
                new MenuItemData { IsSeparator = true },
                new MenuItemData
                {
                    Text = "Theme",
                    Icon = IconHelper.Create(Symbol.SolidStar),
                    SubItems =
                    [
                        this.CreateThemeMenuItem("Light", "Light", Symbol.SolidStar),
                        this.CreateThemeMenuItem("Dark", "Dark", Symbol.OutlineStar),
                        this.CreateThemeMenuItem("High Contrast", "High Contrast", Symbol.Important),
                    ],
                },
            ],
    };

    private MenuItemData CreateNavigateMenu() => new()
    {
        Text = "Navigate",
        SubItems =
            [
                this.CreateMenuCommand("Go Back", Symbol.Back, "Alt+Left", "Navigated back"),
                this.CreateMenuCommand("Go Forward", Symbol.Forward, "Alt+Right", "Navigated forward"),
                this.CreateMenuCommand("Go to Definition", Symbol.Target, "F12", "Navigated to definition"),
                this.CreateMenuCommand("Go to Declaration", Symbol.Target, "Ctrl+F12", "Navigated to declaration"),
                this.CreateMenuCommand("Peek Definition", Symbol.Preview, "Alt+F12", "Opened peek definition"),
                this.CreateMenuCommand("Toggle Editor/Terminal", Symbol.Switch, "Ctrl+`", "Toggled editor and terminal focus"),
                this.CreateMenuCommand("Navigate to Matching Brace", Symbol.GoToStart, "Ctrl+]", "Jumped to matching brace"),
            ],
    };

    private MenuItemData CreateRunMenu() => new()
    {
        Text = "Run",
        SubItems =
            [
                this.CreateMenuCommand("Start Debugging", Symbol.Play, "F5", "Started debugging"),
                this.CreateMenuCommand("Run Without Debugging", Symbol.Play, "Ctrl+F5", "Started without debugging"),
                this.CreateMenuCommand("Stop Debugging", Symbol.Stop, "Shift+F5", "Stopped debugging"),
                this.CreateMenuCommand("Restart Debugging", Symbol.Refresh, "Ctrl+Shift+F5", "Restarted debugging"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Toggle Breakpoint", Symbol.Flag, "F9", "Toggled breakpoint"),
                this.CreateMenuCommand("Step Over", Symbol.Forward, "F10", "Stepped over"),
                this.CreateMenuCommand("Step Into", Symbol.Forward, "F11", "Stepped into"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Open Run Configurations...", Symbol.Setting, null, "Opened run configurations"),
            ],
    };

    private MenuItemData CreateToolsMenu() => new()
    {
        Text = "Tools",
        SubItems =
            [
                this.CreateMenuCommand("Command Palette...", Symbol.Switch, "Ctrl+Shift+P", "Opened command palette"),
                this.CreateMenuCommand("Extensions...", Symbol.Manage, "Ctrl+Shift+X", "Opened extensions manager"),
                this.CreateMenuCommand("Run Task...", Symbol.Sync, "Ctrl+Shift+B", "Opened task runner"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("Toggle Terminal", Symbol.Contact, "Ctrl+`", "Toggled integrated terminal"),
                this.CreateMenuCommand("Open Settings (JSON)", Symbol.Setting, "Ctrl+Shift+,", "Opened settings (JSON)"),
            ],
    };

    private MenuItemData CreateHelpMenu() => new()
    {
        Text = "Help",
        SubItems =
            [
                this.CreateMenuCommand("View Documentation", Symbol.Help, "F1", "Opened documentation"),
                this.CreateMenuCommand("Release Notes", Symbol.Read, null, "Opened release notes"),
                this.CreateMenuCommand("Report Issue...", Symbol.Flag, null, "Opened issue reporter"),
                new MenuItemData { IsSeparator = true },
                this.CreateMenuCommand("About DroidNet Editor", Symbol.Contact, null, "Opened About dialog"),
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

    private MenuItemData CreateToggleMenuItem(string text, Symbol icon, Func<bool> getState, Action<bool> setState)
    {
        var item = new MenuItemData
        {
            Text = text,
            Icon = IconHelper.Create(icon),
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

    private MenuItemData CreateMenuCommand(string text, Symbol icon, string? accelerator, string message) => new()
    {
        Text = text,
        Icon = IconHelper.Create(icon),
        AcceleratorText = accelerator,
        Command = this.CreateCommand(message),
    };

    private MenuItemData CreateThemeMenuItem(string text, string theme, Symbol icon) => new()
    {
        Text = text,
        Icon = IconHelper.Create(icon),
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

// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuItem control with live property editing capabilities.
/// Shows how to dynamically modify MenuItemData properties and see real-time updates.
/// </summary>
public partial class MenuItemDemoViewModel : ObservableObject
{
    private readonly Dictionary<string, string> iconMapping = new(StringComparer.OrdinalIgnoreCase)
    {
        { "None", string.Empty },
        { "Save", "\uE74E" },
        { "Open", "\uE8E5" },
        { "Copy", "\uE8C8" },
        { "Cut", "\uE8C6" },
        { "Paste", "\uE77F" },
        { "Delete", "\uE74D" },
        { "Undo", "\uE7A7" },
        { "Redo", "\uE7A6" },
        { "Settings", "\uE713" },
        { "Print", "\uE749" },
        { "Share", "\uE72D" },
        { "Mail", "\uE715" },
        { "Calendar", "\uE787" },
        { "Contact", "\uE77B" },
        { "Pictures", "\uE91B" },
        { "Document", "\uE8A5" },
        { "Folder", "\uE8B7" },
        { "Home", "\uE80F" },
        { "Refresh", "\uE72C" },
        { "Search", "\uE721" },
        { "Add", "\uE710" },
        { "Remove", "\uE738" },
        { "Edit", "\uE70F" },
        { "View", "\uE890" },
        { "Filter", "\uE71C" },
        { "Sort", "\uE8CB" },
        { "Help", "\uE897" },
        { "Info", "\uE946" },
        { "Warning", "\uE7BA" },
        { "Error", "\uE783" },
    };

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuItemDemoViewModel"/> class.
    /// </summary>
    public MenuItemDemoViewModel()
    {
        this.AvailableIcons = new ObservableCollection<KeyValuePair<string, string>>(this.iconMapping);

        this.MenuItemData = new MenuItemData
        {
            Text = "Sample Menu Item",
            Command = new RelayCommand(this.OnMenuItemExecuted),
            IsEnabled = true,
            AcceleratorText = "Ctrl+S",
            Icon = new FontIconSource { Glyph = this.iconMapping["Save"], FontSize = 16 },
            Mnemonic = 'S',
        };

        this.SelectedIconName = "Save";
        this.MnemonicText = "S";

        // Subscribe to property changes to update dependent properties
        this.PropertyChanged += this.OnPropertyChanged;
    }

    [ObservableProperty]
    public partial string LastActionMessage { get; set; } = "Ready - Click the MenuItem to see command execution feedback.";

    [ObservableProperty]
    public partial MenuItemData MenuItemData { get; set; }

    [ObservableProperty]
    public partial string? SelectedIconName { get; set; }

    [ObservableProperty]
    public partial string MnemonicText { get; set; } = string.Empty;

    /// <summary>
    ///     Gets the available icons for the MenuItem.
    /// </summary>
    public ObservableCollection<KeyValuePair<string, string>> AvailableIcons { get; }

    /// <summary>
    ///     Command to reset the MenuItem to default values.
    /// </summary>
    [RelayCommand]
    private void ResetToDefault()
    {
        this.MenuItemData.Text = "Sample Menu Item";
        this.MenuItemData.AcceleratorText = "Ctrl+S";
        this.MenuItemData.IsEnabled = true;
        this.MenuItemData.IsCheckable = false;
        this.MenuItemData.IsChecked = false;
        this.MenuItemData.IsSeparator = false;
        this.MenuItemData.RadioGroupId = null;
        this.MenuItemData.Mnemonic = 'S';

        this.SelectedIconName = "Save";
        this.MnemonicText = "S";

        this.LastActionMessage = "MenuItem properties reset to default values.";
    }

    /// <summary>
    ///     Handles the MenuItem command execution.
    /// </summary>
    private void OnMenuItemExecuted()
    {
        var timestamp = DateTime.Now.ToString("HH:mm:ss", System.Globalization.CultureInfo.InvariantCulture);

        if (this.MenuItemData.IsSeparator)
        {
            this.LastActionMessage = $"[{timestamp}] Separator clicked - separators typically don't have commands.";
            return;
        }

        if (this.MenuItemData.IsCheckable)
        {
            var state = this.MenuItemData.IsChecked ? "checked" : "unchecked";
            this.LastActionMessage = $"[{timestamp}] Checkable MenuItem '{this.MenuItemData.Text}' executed - now {state}.";
        }
        else
        {
            this.LastActionMessage = $"[{timestamp}] MenuItem '{this.MenuItemData.Text}' command executed successfully!";
        }

        if (!string.IsNullOrEmpty(this.MenuItemData.RadioGroupId))
        {
            this.LastActionMessage += $" (Radio group: {this.MenuItemData.RadioGroupId})";
        }
    }

    /// <summary>
    ///     Handles property changes to update dependent properties.
    /// </summary>
    private void OnPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(this.SelectedIconName):
                this.UpdateIcon();
                break;
            case nameof(this.MnemonicText):
                this.UpdateMnemonic();
                break;
        }
    }

    /// <summary>
    ///     Updates the MenuItem icon based on the selected icon name.
    /// </summary>
    private void UpdateIcon()
    {
        if (this.SelectedIconName is null or "None")
        {
            // There is a bug in WinUI where setting Icon to null then setting
            // it to a new value will throw an exception.
            this.MenuItemData.Icon = null;
        }
        else if (this.iconMapping.TryGetValue(this.SelectedIconName, out var glyph))
        {
            this.MenuItemData.Icon = new FontIconSource { Glyph = glyph, FontSize = 16 };
        }
    }

    /// <summary>
    ///     Updates the MenuItem mnemonic based on the mnemonic text.
    /// </summary>
    private void UpdateMnemonic()
    {
        if (string.IsNullOrEmpty(this.MnemonicText))
        {
            this.MenuItemData.Mnemonic = null;
        }
        else
        {
            this.MenuItemData.Mnemonic = char.ToUpper(this.MnemonicText[0], System.Globalization.CultureInfo.InvariantCulture);
        }
    }
}

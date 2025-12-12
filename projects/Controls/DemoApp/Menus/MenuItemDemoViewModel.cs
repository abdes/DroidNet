// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// Demonstrates the MenuItem control with live property editing capabilities.
/// Shows how to dynamically modify MenuItemData properties and see real-time updates.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public due to source generated ViewModel property")]
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

    private RelayCommand? menuItemCommand;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuItemDemoViewModel"/> class.
    /// </summary>
    /// <param name="loggerFactory">The factory used to create loggers for menu operations.</param>
    public MenuItemDemoViewModel(ILoggerFactory loggerFactory)
    {
        this.AvailableIcons = new ObservableCollection<KeyValuePair<string, string>>(this.iconMapping);

        // Create a command with dynamic CanExecute that we can control
        this.menuItemCommand = new RelayCommand(this.OnMenuItemExecuted, () => this.CommandCanExecute);

        this.MenuItemData = new MenuItemData
        {
            Text = "Sample Menu Item",
            Command = this.menuItemCommand,
            IsEnabled = true,
            AcceleratorText = "Ctrl+S",
            Icon = new FontIconSource { Glyph = this.iconMapping["Save"], FontSize = 16 },
            Mnemonic = 'S',
        };

        this.MenuSource = new MenuBuilder(loggerFactory)
            .AddMenuItem(this.MenuItemData).Build();

        this.SelectedIconName = "Save";
        this.MnemonicText = "S";
        this.CommandCanExecute = true;
        this.UseCommand = true;

        // Subscribe to property changes to update dependent properties
        this.PropertyChanged += this.OnPropertyChanged;
    }

    /// <summary>
    /// Gets the menu source containing the editable MenuItemData.
    /// </summary>
    public IMenuSource MenuSource { get; }

    [ObservableProperty]
    public partial string LastActionMessage { get; set; } = "Ready - Click the MenuItem to see command execution feedback.";

    [ObservableProperty]
    public partial MenuItemData MenuItemData { get; set; }

    [ObservableProperty]
    public partial string? SelectedIconName { get; set; }

    [ObservableProperty]
    public partial string MnemonicText { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets a value indicating whether the command can execute.
    /// This demonstrates how MenuItem visual state updates when CanExecute changes.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CommandExecutabilityStatus))]
    public partial bool CommandCanExecute { get; set; } = true;

    /// <summary>
    /// Gets or sets a value indicating whether a command should be attached to the MenuItem.
    /// When false, the MenuItem operates without a command.
    /// </summary>
    [ObservableProperty]
    public partial bool UseCommand { get; set; } = true;

    /// <summary>
    /// Gets a user-friendly status message about the command's executability.
    /// </summary>
    public string CommandExecutabilityStatus => this.CommandCanExecute
        ? "✅ Command can execute - Item is interactive"
        : "🚫 Command cannot execute - Item appears disabled";

    /// <summary>
    ///     Gets the available icons for the MenuItem.
    /// </summary>
    public ObservableCollection<KeyValuePair<string, string>> AvailableIcons { get; }

    /// <summary>
    ///     Handles the MenuItem Invoked event.
    ///     This is called when the MenuItem is invoked, regardless of whether a command is attached.
    /// </summary>
    /// <param name="e">The event arguments containing invocation details.</param>
    public void OnMenuItemInvoked(MenuItemInvokedEventArgs e)
    {
        var timestamp = DateTime.Now.ToString("HH:mm:ss", System.Globalization.CultureInfo.InvariantCulture);

        if (e.IsFailed)
        {
            this.LastActionMessage = $"[{timestamp}] ❌ MenuItem '{e.ItemData.Text}' invocation FAILED: {e.Exception?.Message}";
            return;
        }

        // If there's a command, the command handler already updated the message
        // Only update if there's no command (command handler won't run)
        if (e.ItemData.Command == null)
        {
            if (e.ItemData.IsSeparator)
            {
                this.LastActionMessage = $"[{timestamp}] Separator invoked via Invoked event (no command).";
            }
            else if (e.ItemData.IsCheckable || !string.IsNullOrEmpty(e.ItemData.RadioGroupId))
            {
                var state = e.ItemData.IsChecked ? "checked" : "unchecked";
                this.LastActionMessage = $"[{timestamp}] ✅ MenuItem '{e.ItemData.Text}' invoked via Invoked event (no command) - now {state}.";
            }
            else
            {
                this.LastActionMessage = $"[{timestamp}] ✅ MenuItem '{e.ItemData.Text}' invoked via Invoked event (no command attached).";
            }
        }
    }

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
        this.UseCommand = true;
        this.CommandCanExecute = true;

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
            case nameof(this.CommandCanExecute):
                // Notify the command that CanExecute has changed
                this.menuItemCommand?.NotifyCanExecuteChanged();
                break;
            case nameof(this.UseCommand):
                this.UpdateCommandAttachment();
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

    /// <summary>
    ///     Updates whether a command is attached to the MenuItem.
    /// </summary>
    private void UpdateCommandAttachment()
    {
        if (this.UseCommand)
        {
            this.menuItemCommand ??= new RelayCommand(this.OnMenuItemExecuted, () => this.CommandCanExecute);
            this.MenuItemData.Command = this.menuItemCommand;
            this.LastActionMessage = "Command attached - MenuItem executability is controlled by command's CanExecute.";
        }
        else
        {
            this.MenuItemData.Command = null;
            this.LastActionMessage = "Command removed - MenuItem operates without a command.";
        }
    }
}

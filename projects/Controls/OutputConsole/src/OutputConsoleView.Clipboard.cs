// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.Windows.Storage.Pickers;
using Windows.ApplicationModel.DataTransfer;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Copying, saving and the row context menu.
/// </summary>
public sealed partial class OutputConsoleView
{
    private static readonly UTF8Encoding Utf8WithoutBom = new(encoderShouldEmitUTF8Identifier: false);

    private OutputLogRow? menuRow;

    private static void SetClipboardText(string text)
    {
        var package = new DataPackage();
        package.SetText(text);
        Clipboard.SetContent(package);
    }

    private static async Task CopyEntriesAsync(IReadOnlyList<OutputLogEntry> entries)
    {
        if (entries.Count == 0)
        {
            return;
        }

        // Entries are immutable, so they can be formatted off the UI thread.
        var text = await Task.Run(() => OutputLogFormatter.Format(entries)).ConfigureAwait(true);
        SetClipboardText(text);
    }

    private static OutputLogRowView? RowViewOf(object? source)
    {
        var element = source as DependencyObject;
        while (element is not null and not OutputLogRowView)
        {
            element = VisualTreeHelper.GetParent(element);
        }

        return element as OutputLogRowView;
    }

    private void WireRowMenu()
    {
        this.CopyMenuItem.Click += async (_, _) => await this.CopySelectionAsync().ConfigureAwait(true);
        this.CopyMessageMenuItem.Click += (_, _) => this.CopyMessages();
        this.CopyVisibleMenuItem.Click += async (_, _) => await this.CopyVisibleAsync().ConfigureAwait(true);
        this.CopyAllMenuItem.Click += async (_, _) => await CopyEntriesAsync(this.AllEntries()).ConfigureAwait(true);
        this.ShowOnlySubsystemMenuItem.Click += (_, _) => this.ShowOnlySubsystem(this.menuRow?.First.Subsystem ?? string.Empty);
        this.HideSubsystemMenuItem.Click += (_, _) => this.SetSubsystemShown(this.menuRow?.First.Subsystem ?? string.Empty, shown: false);
        this.ShowDetailsMenuItem.Click += (_, _) => this.ShowDetails();
        this.SelectAllMenuItem.Click += (_, _) => this.List.SelectAll();
    }

    private Task CopySelectionAsync() => CopyEntriesAsync(this.CaptureSelection());

    private Task CopyVisibleAsync() => CopyEntriesAsync([.. this.viewItems.SelectMany(static row => row.Entries)]);

    private void CopyMessages()
    {
        var builder = new StringBuilder();
        foreach (var row in this.SelectedRowsInOrder())
        {
            _ = builder.Append(row.First.Message).Append(Environment.NewLine);
        }

        if (builder.Length > 0)
        {
            SetClipboardText(builder.ToString());
        }
    }

    private void CopyDetails()
    {
        if (this.detailsRow is { } row)
        {
            var builder = new StringBuilder();
            OutputLogFormatter.AppendDetails(builder, row.Last);
            SetClipboardText(builder.ToString());
        }
    }

    private IReadOnlyList<OutputLogEntry> AllEntries()
        => this.buffer is { } source
            ? source.Snapshot()
            : this.ItemsSource is IEnumerable<OutputLogEntry> items ? [.. items] : [];

    private async Task SaveOutputAsync()
    {
        if (this.XamlRoot?.ContentIslandEnvironment is not { } island)
        {
            return;
        }

        var entries = this.AllEntries();
        var picker = new FileSavePicker(island.AppWindowId)
        {
            SuggestedFileName = string.Create(CultureInfo.InvariantCulture, $"output-{DateTime.Now:yyyyMMdd-HHmmss}"),
        };
        picker.FileTypeChoices.Add("Log file", [".log"]);
        picker.FileTypeChoices.Add("Text file", [".txt"]);

        var picked = await picker.PickSaveFileAsync();
        if (picked is null)
        {
            return;
        }

        try
        {
            var text = await Task.Run(() => OutputLogFormatter.Format(entries)).ConfigureAwait(true);
            await File.WriteAllTextAsync(picked.Path, text, Utf8WithoutBom).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            this.ErrorInfoBar.Title = "Couldn't save the output";
            this.ErrorInfoBar.Message = string.Create(CultureInfo.CurrentCulture, $"{picked.Path}: {ex.Message}");
            this.ErrorInfoBar.IsOpen = true;
        }
    }

    private void OnListContextRequested(UIElement sender, ContextRequestedEventArgs args)
    {
        this.menuRow = RowViewOf(args.OriginalSource)?.Row ?? this.lastSelectedRow;
        if (this.menuRow is { } row && !this.List.SelectedItems.Contains(row))
        {
            this.List.SelectedItem = row;
        }
    }

    private void PrepareRowMenu()
    {
        var hasRow = this.menuRow is not null;
        var subsystem = this.menuRow?.First.Subsystem;
        var showSubsystemItems = subsystem is not null ? Visibility.Visible : Visibility.Collapsed;
        this.SubsystemMenuSeparator.Visibility = showSubsystemItems;
        this.ShowOnlySubsystemMenuItem.Visibility = showSubsystemItems;
        this.HideSubsystemMenuItem.Visibility = showSubsystemItems;
        this.ShowOnlySubsystemMenuItem.Text = $"Show only '{subsystem}'";
        this.HideSubsystemMenuItem.Text = $"Hide '{subsystem}'";
        this.CopyMenuItem.IsEnabled = hasRow;
        this.CopyMessageMenuItem.IsEnabled = hasRow;
        this.ShowDetailsMenuItem.IsEnabled = hasRow;
    }

    private async void OnCopyAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        await this.CopySelectionAsync().ConfigureAwait(true);
    }

    private async void OnCopyVisibleAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        await this.CopyVisibleAsync().ConfigureAwait(true);
    }
}

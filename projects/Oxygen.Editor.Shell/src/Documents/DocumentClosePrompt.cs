// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// Asks, in one window, whether to save the unsaved documents that close: <c>Save changes to 2 documents?</c> with
/// <b>Save</b>, <b>Don't Save</b> and <b>Cancel</b>. Each document is listed with its asset, and when there are several
/// a check box chooses which ones Save saves. A document whose file changed on disk offers to reload it or save a copy.
/// </summary>
/// <param name="dialogs">The dialog service.</param>
/// <param name="windows">The window manager, which finds the window.</param>
/// <param name="windowId">The window the dialog belongs to.</param>
public sealed class DocumentClosePrompt(IDialogService dialogs, IWindowManagerService windows, WindowId windowId) : IDocumentClosePrompt
{
    /// <inheritdoc/>
    public Task<bool> ConfirmAsync(IReadOnlyList<DocumentCloseItem> documents)
    {
        ArgumentNullException.ThrowIfNull(documents);
        var window = windows.GetWindow(windowId)
            ?? throw new InvalidOperationException("The document's window is no longer available.");
        return window.DispatcherQueue.EnqueueAsync(() => this.ShowAsync(documents));
    }

    private static string QuestionOf(IReadOnlyList<DocumentCloseItem> documents)
        => documents.Count == 1
            ? $"Save changes to “{documents[0].Tab.Title}”?"
            : string.Create(CultureInfo.CurrentCulture, $"Save changes to {documents.Count} documents?");

    private static CloseRow CreateRow(DocumentCloseItem item, bool canChoose)
    {
        var content = new StackPanel { Spacing = 2 };
        var selection = new CheckBox
        {
            Content = item.Tab.Title,
            IsChecked = true,
            Visibility = canChoose ? Visibility.Visible : Visibility.Collapsed,
        };
        AutomationProperties.SetName(selection, $"Save {item.Tab.Title}");
        selection.Checked += (_, _) => item.IsSelected = true;
        selection.Unchecked += (_, _) => item.IsSelected = false;
        content.Children.Add(selection);
        content.Children.Add(new TextBlock
        {
            Text = item.Tab.Document.File.Asset.VirtualPath,
            TextWrapping = TextWrapping.Wrap,
            Opacity = 0.7,
        });

        var status = new TextBlock { TextWrapping = TextWrapping.Wrap, Visibility = Visibility.Collapsed };
        AutomationProperties.SetLiveSetting(status, AutomationLiveSetting.Polite);
        content.Children.Add(status);
        var conflicts = new DocumentConflictPanel(item, () =>
        {
            status.Visibility = Visibility.Collapsed;
            selection.IsEnabled = !item.IsSaved;
        });
        content.Children.Add(conflicts);
        return new CloseRow(item, selection, status, content, conflicts);
    }

    private static ScrollViewer CreateContent(IReadOnlyList<CloseRow> rows)
    {
        var list = new StackPanel { Spacing = 12, MinWidth = 320 };
        foreach (var row in rows)
        {
            list.Children.Add(row.Content);
        }

        return new ScrollViewer
        {
            Content = list,
            MaxHeight = 320,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
    }

    private static async Task<bool> SaveSelectedAsync(IReadOnlyList<CloseRow> rows)
    {
        foreach (var row in rows)
        {
            row.Selection.IsEnabled = false;
        }

        try
        {
            var allSaved = true;
            foreach (var row in rows.Where(row => row.Item.IsSelected))
            {
                var saved = await row.Item.SaveAsync().ConfigureAwait(true);
                row.Status.Text = saved ? "Saved" : "Save failed. Your changes are still open. See Output for details.";
                row.Status.Visibility = row.Item.HasConflict ? Visibility.Collapsed : Visibility.Visible;
                row.Conflicts.Refresh();
                allSaved &= saved;
            }

            return allSaved;
        }
        finally
        {
            foreach (var row in rows)
            {
                row.Selection.IsEnabled = !row.Item.IsSaved;
            }
        }
    }

    private async Task<bool> ShowAsync(IReadOnlyList<DocumentCloseItem> documents)
    {
        var rows = documents.Select(item => CreateRow(item, canChoose: documents.Count > 1)).ToArray();
        var spec = new DialogSpec(QuestionOf(documents), CreateContent(rows))
        {
            PrimaryButtonText = "Save",
            SecondaryButtonText = "Don't Save",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Close,
            PrimaryAction = () => SaveSelectedAsync(rows),
        };
        DialogButton result;
        try
        {
            result = await dialogs.ShowAsync(spec, windowId).ConfigureAwait(true);
        }
        finally
        {
            await Task.WhenAll(rows.Select(row => row.Conflicts.Pending)).ConfigureAwait(true);
            await Task.WhenAll(documents.Select(item => item.Pending)).ConfigureAwait(true);
        }

        if (result == DialogButton.Secondary)
        {
            foreach (var item in documents)
            {
                item.IsSelected = false;
            }
        }

        return result is DialogButton.Primary or DialogButton.Secondary;
    }

    private sealed record CloseRow(DocumentCloseItem Item, CheckBox Selection, TextBlock Status, StackPanel Content, DocumentConflictPanel Conflicts);
}

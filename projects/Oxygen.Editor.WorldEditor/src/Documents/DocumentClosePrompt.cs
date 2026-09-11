// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;

namespace Oxygen.Editor.World.Documents;

/// <summary>Presents the editor's single-document and combined unsaved-document decisions.</summary>
/// <param name="dialogs">The shared, serialized Aura dialog service.</param>
/// <param name="windows">The owner-window resolver.</param>
public sealed class DocumentClosePrompt(IDialogService dialogs, IWindowManagerService windows) : IDocumentClosePrompt
{
    /// <inheritdoc/>
    public Task<bool> ConfirmAsync(WindowId windowId, IReadOnlyList<DocumentCloseItem> documents, bool isWorkspaceClose)
    {
        var window = windows.GetWindow(windowId)
            ?? throw new InvalidOperationException("The document's window is no longer available.");
        return window.DispatcherQueue.EnqueueAsync(() => this.ShowAsync(windowId, documents, isWorkspaceClose));
    }

    private static CloseRow CreateRow(DocumentCloseItem item, bool isWorkspaceClose)
    {
        var content = new StackPanel { Spacing = 2 };
        var selection = new CheckBox
        {
            Content = item.Metadata.Title,
            IsChecked = true,
            Visibility = isWorkspaceClose ? Visibility.Visible : Visibility.Collapsed,
        };
        AutomationProperties.SetName(selection, $"Save {item.Metadata.Title}");
        selection.Checked += (_, _) => item.IsSelected = true;
        selection.Unchecked += (_, _) => item.IsSelected = false;
        content.Children.Add(selection);
        if (isWorkspaceClose && item.Metadata is MaterialDocumentMetadata material)
        {
            content.Children.Add(new TextBlock { Text = material.MaterialUri.ToString(), TextWrapping = TextWrapping.Wrap });
        }

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

    private static StackPanel CreateContent(IReadOnlyList<CloseRow> rows, bool isWorkspaceClose)
    {
        var content = new StackPanel { Spacing = 12, MinWidth = 320 };
        content.Children.Add(new TextBlock
        {
            Text = isWorkspaceClose
                ? "Select the documents to save before closing. Changes in unchecked documents will be discarded."
                : $"Save changes to “{rows[0].Item.Metadata.Title}” before closing?",
            TextWrapping = TextWrapping.Wrap,
        });
        var list = new StackPanel { Spacing = 8 };
        foreach (var row in rows)
        {
            list.Children.Add(row.Content);
        }

        content.Children.Add(new ScrollViewer
        {
            Content = list,
            MaxHeight = 320,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        });
        return content;
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

    private async Task<bool> ShowAsync(WindowId windowId, IReadOnlyList<DocumentCloseItem> documents, bool isWorkspaceClose)
    {
        var rows = documents.Select(item => CreateRow(item, isWorkspaceClose)).ToArray();
        var spec = new DialogSpec(isWorkspaceClose ? "Close workspace?" : "Unsaved changes", CreateContent(rows, isWorkspaceClose))
        {
            PrimaryButtonText = isWorkspaceClose ? "Save Selected" : "Save",
            SecondaryButtonText = isWorkspaceClose ? "Discard All" : "Discard",
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

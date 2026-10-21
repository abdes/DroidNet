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
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>The confirmations of one window, asked as dialogs of that window.</summary>
/// <param name="dialogs">The dialog service.</param>
/// <param name="windows">The window manager, which finds the window.</param>
/// <param name="windowId">The window the dialogs belong to.</param>
public sealed class DocumentConfirmations(IDialogService dialogs, IWindowManagerService windows, WindowId windowId) : IDocumentConfirmations
{
    /// <inheritdoc/>
    public Task<bool> ConfirmReloadAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.AskAsync(() => new DialogSpec(
            $"Discard your changes to {document.Title} and reload?",
            new TextBlock { Text = "Its Undo/Redo history is discarded too.", TextWrapping = TextWrapping.Wrap })
        {
            PrimaryButtonText = "Reload",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Close,
        });
    }

    /// <inheritdoc/>
    public Task<bool> ConfirmSaveBeforeAsync(IReadOnlyList<UnsavedDocument> documents, SaveBeforeOperation operation)
    {
        ArgumentNullException.ThrowIfNull(documents);
        var (button, verb, why) = operation switch
        {
            SaveBeforeOperation.Play => ("Play", "play", "Play cooks the saved files."),
            SaveBeforeOperation.Cook => ("Cook", "cook", "The cook reads the saved files."),
            SaveBeforeOperation.Rename => ("Rename", "rename", "Renaming rewrites the files that use it."),
            SaveBeforeOperation.Move => ("Move", "move", "Moving rewrites the files that use it."),
            _ => throw new ArgumentOutOfRangeException(nameof(operation), operation, message: null),
        };
        var count = documents.Count == 1 ? "1 document" : string.Create(CultureInfo.CurrentCulture, $"{documents.Count} documents");
        return this.AskAsync(() => new DialogSpec($"Save {count} and {verb}?", CreateDocumentList(why, documents))
        {
            PrimaryButtonText = $"Save and {button}",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
        });
    }

    /// <inheritdoc/>
    public Task<string?> AskCopyNameAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.OnWindowAsync(async () =>
        {
            var name = new TextBox { Header = "Name", Text = document.Title + " Copy" };
            AutomationProperties.SetName(name, $"Name of the copy of {document.Title}");
            var content = new StackPanel { Spacing = 8, MinWidth = 320 };
            content.Children.Add(name);
            content.Children.Add(new TextBlock
            {
                Text = "The copy has its own asset identity. The original document and existing references stay unchanged.",
                TextWrapping = TextWrapping.Wrap,
            });
            var result = await dialogs.ShowAsync(
                new DialogSpec($"Save a copy of {document.Title}", content)
                {
                    PrimaryButtonText = "Save a Copy",
                    CloseButtonText = "Cancel",
                    DefaultButton = DialogButton.Primary,
                },
                windowId).ConfigureAwait(true);
            return result == DialogButton.Primary && !string.IsNullOrWhiteSpace(name.Text) ? name.Text : null;
        });
    }

    private static ScrollViewer CreateDocumentList(string why, IReadOnlyList<UnsavedDocument> documents)
    {
        var list = new StackPanel { Spacing = 8, MinWidth = 320 };
        list.Children.Add(new TextBlock { Text = why, TextWrapping = TextWrapping.Wrap });
        foreach (var document in documents)
        {
            var row = new StackPanel { Spacing = 2 };
            row.Children.Add(new TextBlock { Text = document.Title, TextWrapping = TextWrapping.Wrap });
            row.Children.Add(new TextBlock { Text = document.File.Asset.VirtualPath, TextWrapping = TextWrapping.Wrap, Opacity = 0.7 });
            list.Children.Add(row);
        }

        return new ScrollViewer
        {
            Content = list,
            MaxHeight = 320,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
    }

    // The dialog's content is built on the window's thread, which owns it.
    private Task<bool> AskAsync(Func<DialogSpec> spec)
        => this.OnWindowAsync(async () => await dialogs.ShowAsync(spec(), windowId).ConfigureAwait(true) == DialogButton.Primary);

    private Task<T> OnWindowAsync<T>(Func<Task<T>> show)
    {
        var window = windows.GetWindow(windowId) ?? throw new InvalidOperationException("The document's window is no longer available.");
        return window.DispatcherQueue.EnqueueAsync(show);
    }
}

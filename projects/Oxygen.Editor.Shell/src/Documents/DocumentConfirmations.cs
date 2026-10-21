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

/// <summary>The confirmations of the editor's windows, each asked as a dialog of the window that asks it.</summary>
/// <param name="dialogs">The dialog service.</param>
/// <param name="windows">The window manager, which finds the window.</param>
public sealed class DocumentConfirmations(IDialogService dialogs, IWindowManagerService windows) : IDocumentConfirmations
{
    /// <inheritdoc/>
    public Task<bool> ConfirmReloadAsync(WindowId window, Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.AskAsync(window, () => new DialogSpec(
            $"Discard your changes to {document.Title} and reload?",
            new TextBlock { Text = "Its Undo/Redo history is discarded too.", TextWrapping = TextWrapping.Wrap })
        {
            PrimaryButtonText = "Reload",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Close,
        });
    }

    /// <inheritdoc/>
    public Task<bool> ConfirmSaveBeforeAsync(WindowId window, IReadOnlyList<UnsavedDocument> documents, SaveBeforeOperation operation)
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
        return this.AskAsync(window, () => new DialogSpec($"Save {count} and {verb}?", CreateDocumentList(why, documents))
        {
            PrimaryButtonText = $"Save and {button}",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
        });
    }

    /// <inheritdoc/>
    public Task<string?> AskCopyNameAsync(WindowId window, Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.OnWindowAsync(window, async () =>
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
                window).ConfigureAwait(true);
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
    private Task<bool> AskAsync(WindowId window, Func<DialogSpec> spec)
        => this.OnWindowAsync(window, async () => await dialogs.ShowAsync(spec(), window).ConfigureAwait(true) == DialogButton.Primary);

    private Task<T> OnWindowAsync<T>(WindowId window, Func<Task<T>> show)
    {
        var owner = windows.GetWindow(window) ?? throw new InvalidOperationException("The document's window is no longer available.");
        return owner.DispatcherQueue.EnqueueAsync(show);
    }
}

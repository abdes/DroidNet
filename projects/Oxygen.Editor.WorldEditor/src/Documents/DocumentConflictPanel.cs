// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>Inline conflict actions with explicit discard confirmation and no nested dialogs.</summary>
internal sealed partial class DocumentConflictPanel : UserControl
{
    private readonly DocumentCloseItem item;
    private readonly StackPanel actions = new() { Orientation = Orientation.Horizontal, Spacing = 8 };
    private readonly StackPanel confirmation = new() { Spacing = 8, Visibility = Visibility.Collapsed };
    private readonly TextBlock status = new() { TextWrapping = TextWrapping.Wrap };
    private readonly Action changed;
    private bool available;

    /// <summary>Initializes a new instance of the <see cref="DocumentConflictPanel"/> class.</summary>
    /// <param name="item">The document and its queued operations.</param>
    /// <param name="changed">Refreshes the containing prompt after an action.</param>
    public DocumentConflictPanel(DocumentCloseItem item, Action changed)
    {
        this.item = item;
        this.changed = changed;
        var content = new StackPanel { Spacing = 8 };
        this.Content = content;
        content.Children.Add(new TextBlock { Text = "The file changed outside this document. Reload it, save a separate copy, or cancel closing to keep your edits.", TextWrapping = TextWrapping.Wrap });
        content.Children.Add(this.actions);
        content.Children.Add(this.confirmation);
        content.Children.Add(this.status);
        AutomationProperties.SetLiveSetting(this.status, AutomationLiveSetting.Polite);
        var reload = new Button { Content = "Reload" };
        reload.Click += (_, _) => this.ShowReloadConfirmation();
        var copy = new Button { Content = "Save Copy" };
        copy.Click += (_, _) => this.ShowCopyInput();
        this.actions.Children.Add(reload);
        this.actions.Children.Add(copy);
        this.Loaded += (_, _) => this.available = true;
        this.Unloaded += (_, _) => this.available = false;
        this.Refresh();
    }

    /// <summary>Gets completion of the current inline action.</summary>
    public Task Pending { get; private set; } = Task.CompletedTask;

    /// <summary>Refreshes conflict visibility after a save attempt.</summary>
    public void Refresh()
    {
        this.Visibility = this.item.HasConflict ? Visibility.Visible : Visibility.Collapsed;
        this.actions.Visibility = Visibility.Visible;
        this.confirmation.Visibility = Visibility.Collapsed;
        this.status.Text = string.Empty;
    }

    private void ShowReloadConfirmation()
    {
        this.BeginConfirmation();
        this.confirmation.Children.Add(new TextBlock
        {
            Text = $"Discard unsaved changes and Undo/Redo history for “{this.item.Metadata.Title}” and reload its file from disk?",
            TextWrapping = TextWrapping.Wrap,
        });
        var accept = new Button { Content = "Discard changes and reload" };
        accept.Click += (_, _) => this.Pending = this.RunAsync(this.item.ReloadAsync);
        this.AddConfirmationButtons(accept, "Keep changes");
    }

    private void ShowCopyInput()
    {
        this.BeginConfirmation();
        var name = new TextBox { Header = "New copy name", Text = this.item.SuggestedCopyName };
        AutomationProperties.SetName(name, $"New copy name for {this.item.Metadata.Title}");
        this.confirmation.Children.Add(name);
        this.confirmation.Children.Add(new TextBlock
        {
            Text = "The copy has its own asset identity. The original document and existing references stay unchanged.",
            TextWrapping = TextWrapping.Wrap,
        });
        var accept = new Button { Content = "Save Copy" };
        accept.Click += (_, _) => this.Pending = this.RunAsync(() => this.item.SaveCopyAsync(name.Text));
        this.AddConfirmationButtons(accept, "Cancel copy");
    }

    private void BeginConfirmation()
    {
        this.status.Text = string.Empty;
        this.actions.Visibility = Visibility.Collapsed;
        this.confirmation.Children.Clear();
        this.confirmation.Visibility = Visibility.Visible;
    }

    private void AddConfirmationButtons(Button accept, string cancelText)
    {
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        var cancel = new Button { Content = cancelText };
        cancel.Click += (_, _) => this.Refresh();
        buttons.Children.Add(accept);
        buttons.Children.Add(cancel);
        this.confirmation.Children.Add(buttons);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "An inline action must show its failure and keep the document open instead of escaping an async UI event.")]
    private async Task RunAsync(Func<Task<DocumentConflictResult>> action)
    {
        if (!this.available || !this.IsEnabled)
        {
            return;
        }

        this.IsEnabled = false;
        try
        {
            var result = await action().ConfigureAwait(true);
            this.status.Text = result.Message;
            if (result.Succeeded)
            {
                this.confirmation.Visibility = Visibility.Collapsed;
                this.actions.Visibility = this.item.HasConflict ? Visibility.Visible : Visibility.Collapsed;
            }

            this.changed();
        }
        catch (Exception exception)
        {
            this.status.Text = $"The action failed. Your document is still open. {exception.Message}";
        }
        finally
        {
            this.IsEnabled = true;
        }
    }
}

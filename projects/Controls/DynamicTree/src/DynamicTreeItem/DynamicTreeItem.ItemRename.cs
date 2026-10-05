// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Represents an item within a dynamic tree structure, supporting on-demand loading of child items,
///     expansion and collapse, selection handling, in-place renaming, and hierarchical indentation.
/// </summary>
public partial class DynamicTreeItem
{
    private Popup? inPlaceRenamePart;
    private bool isContextMenuOpen;
    private ContentPresenter? itemContentPart;
    private TextBlock? itemNameTextBlock;
    private FontIcon? itemNameErrorGlyph;
    private TextBox? itemNameTextBox;
    private bool newNameIsValid;
    private bool renameCommitInProgress;
    private ITreeItem? renameTarget;
    private string? oldItemName;
    private long renameGeneration;

    /// <summary>
    ///     Starts in-place renaming for this tree item.
    /// </summary>
    /// <returns><see langword="true"/> if rename UI was opened; otherwise <see langword="false"/>.</returns>
    public bool BeginRename()
    {
        this.UpdateAncestorReference();
        if (this.itemNameTextBlock is null || this.itemNameTextBox is null || this.ItemAdapter is null || this.renameTarget is not null)
        {
            return false;
        }

        this.itemNameTextBox.Text = this.ItemAdapter?.Label ?? string.Empty;
        this.renameTarget = this.ItemAdapter;
        this.renameGeneration++;
        this.oldItemName = this.renameTarget?.Label;
        this.newNameIsValid = this.renameTarget?.ValidateItemName(this.itemNameTextBox.Text) == true;
        this.renameCommitInProgress = false;
        this.SetRenameError(message: null);
        _ = VisualStateManager.GoToState(this, this.newNameIsValid ? NameIsValidVisualState : NameIsInvalidVisualState, useTransitions: false);
        this.itemNameTextBox.Visibility = Visibility.Visible;
        this.itemNameTextBox.SelectAll();

        // Open the rename popup while the label is still visible so it anchors to the label's
        // position; only then hide the label underneath it.
#pragma warning disable IDE0031 // cannot be simplified
        if (this.inPlaceRenamePart != null)
        {
            this.inPlaceRenamePart.IsOpen = true;
        }
#pragma warning restore IDE0031
        this.itemNameTextBlock.Visibility = Visibility.Collapsed;

        _ = this.itemNameTextBox.Focus(FocusState.Programmatic);

        this.itemNameTextBox.TextChanged += this.RenameTextBox_TextChanged;
        this.itemNameTextBox.LostFocus += this.RenameTextBox_LostFocus;
        this.itemNameTextBox.KeyDown += this.RenameTextBox_KeyDown;
        this.itemNameTextBox.GotFocus += this.RenameTextBox_GotFocus;
        this.itemNameTextBox.ContextMenuOpening += this.RenameTextBox_ContextMenuOpening;

        return true;
    }

    /// <summary>Settles a rename before its owner opens another row's context menu.</summary>
    /// <returns>Whether no uncommitted rename remains.</returns>
    internal async Task<bool> SettleRenameForContextAsync()
    {
        if (this.renameTarget is null)
        {
            return true;
        }

        await this.CommitRenameDraftAsync().ConfigureAwait(true);
        return this.renameTarget is null;
    }

    /// <summary>Commits the current draft and ignores completions belonging to an ended rename session.</summary>
    /// <returns>A task that completes when the commit operation finishes.</returns>
    internal async Task CommitRenameDraftAsync()
    {
        Debug.Assert(
            this.itemNameTextBox is not null,
            "event handler should not be setup if parts are missing");

        if (this.renameCommitInProgress || this.renameTarget is not { } target)
        {
            return;
        }

        this.renameCommitInProgress = true;
        var generation = this.renameGeneration;
        var editor = this.itemNameTextBox;
        editor.IsReadOnly = true;

        try
        {
            var trimmed = this.itemNameTextBox.Text.Trim();
            var result = this.treeControl is { } tree
                ? await tree.CommitRenameAsync(target, trimmed).ConfigureAwait(true)
                : await CommitDirectlyAsync(target, trimmed).ConfigureAwait(true);

            if (generation != this.renameGeneration || !ReferenceEquals(this.renameTarget, target)
                || !ReferenceEquals(this.itemNameTextBox, editor))
            {
                return;
            }

            if (result.Succeeded)
            {
                this.EndRename();
            }
            else
            {
                this.ShowRenameError(editor, result.ErrorMessage);
            }
        }
        catch (Exception exception) when (exception is InvalidOperationException or ArgumentException)
        {
            if (generation != this.renameGeneration || !ReferenceEquals(this.renameTarget, target)
                || !ReferenceEquals(this.itemNameTextBox, editor))
            {
                return;
            }

            this.ShowRenameError(editor, exception.Message);
        }
    }

    private static Task<TreeItemRenameResult> CommitDirectlyAsync(ITreeItem item, string name)
    {
        if (!item.ValidateItemName(name))
        {
            return Task.FromResult(TreeItemRenameResult.Rejected("The name is not valid."));
        }

        item.Label = name;
        return Task.FromResult(TreeItemRenameResult.Success);
    }

    private void ShowRenameError(TextBox editor, string? message)
    {
        this.renameCommitInProgress = false;
        editor.IsReadOnly = false;
        this.SetRenameError(message);
        if (this.inPlaceRenamePart is { } popup)
        {
            popup.IsOpen = true;
        }

        _ = VisualStateManager.GoToState(this, NameIsInvalidVisualState, useTransitions: true);
        _ = editor.Focus(FocusState.Programmatic);
    }

    private void SetupItemNameParts()
    {
        if (this.renameTarget is not null)
        {
            this.CancelRename();
        }

        this.itemNameTextBlock?.DoubleTapped -= this.StartRenameItem;

        this.itemContentPart = this.GetTemplateChild(ContentPresenterPart) as ContentPresenter;
        if (this.itemContentPart is null)
        {
            return;
        }

        this.inPlaceRenamePart = this.GetTemplateChild(InPlaceRenamePart) as Popup;
        this.itemNameTextBlock = this.GetTemplateChild(ItemNamePart) as TextBlock;
        this.itemNameTextBox = this.GetTemplateChild(ItemNameEditPart) as TextBox;
        this.itemNameErrorGlyph = this.GetTemplateChild(ItemNameErrorPart) as FontIcon;
        if (this.itemNameTextBlock is not null && this.itemNameTextBox is not null)
        {
            this.itemNameTextBlock.DoubleTapped += this.StartRenameItem;
            this.itemNameTextBlock.SetBinding(TextBlock.TextProperty, new Microsoft.UI.Xaml.Data.Binding
            {
                Source = this.ItemAdapter,
                Path = new PropertyPath(this.ItemAdapter is TreeItemAdapter ? nameof(TreeItemAdapter.DisplayLabel) : nameof(ITreeItem.Label)),
                Mode = Microsoft.UI.Xaml.Data.BindingMode.OneWay,
            });
        }
    }

    private void RenameTextBox_LostFocus(object sender, RoutedEventArgs e)
    {
        // Do not commit the rename if we're losing focus because of the TextBox
        // context menu opening on right tap.
        if (this.isContextMenuOpen)
        {
            return;
        }

        if (this.newNameIsValid)
        {
            this.TryCommitRename();
        }
        else
        {
            this.CancelRename();
        }
    }

    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "the visual state is used to indicate whether the label is valid or not")]
    private async void TryCommitRename() => await this.CommitRenameDraftAsync().ConfigureAwait(true);

    private void StartRenameItem(object sender, DoubleTappedRoutedEventArgs e)
    {
        _ = sender; // unused

        e.Handled = true;
        _ = this.BeginRename();
    }

    private void EndRename()
    {
        this.renameGeneration++;
        Debug.Assert(
            this.itemNameTextBlock is not null && this.itemNameTextBox is not null,
            "event handler should not be setup if parts are missing");

        // Unsubscribe from events
        this.itemNameTextBox.TextChanged -= this.RenameTextBox_TextChanged;
        this.itemNameTextBox.LostFocus -= this.RenameTextBox_LostFocus;
        this.itemNameTextBox.KeyDown -= this.RenameTextBox_KeyDown;
        this.itemNameTextBox.GotFocus -= this.RenameTextBox_GotFocus;
        this.itemNameTextBox.ContextMenuOpening -= this.RenameTextBox_ContextMenuOpening;
        this.renameTarget = null;
        this.renameCommitInProgress = false;
        this.itemNameTextBox.IsReadOnly = false;
        this.SetRenameError(message: null);
        _ = VisualStateManager.GoToState(this, NameIsValidVisualState, useTransitions: false);

        this.itemNameTextBox.Visibility = Visibility.Collapsed;
        this.itemNameTextBlock.Visibility = Visibility.Visible;

#pragma warning disable IDE0031 // cannot be simplified
        if (this.inPlaceRenamePart != null)
        {
            this.inPlaceRenamePart.IsOpen = false;
        }
#pragma warning restore IDE0031

        // Re-focus selected list item
        _ = this.Focus(FocusState.Programmatic);
    }

    private void CancelRename()
    {
        this.itemNameTextBox!.Text = this.oldItemName;
        this.EndRename();
        this.SetRenameError(message: null);
        _ = VisualStateManager.GoToState(this, NameIsValidVisualState, useTransitions: true);
    }

    private void RenameTextBox_TextChanged(object sender, TextChangedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.newNameIsValid = this.renameTarget?.ValidateItemName(this.itemNameTextBox!.Text.Trim()) == true;
        this.SetRenameError(this.newNameIsValid ? null : "The name is not valid.");
        _ = VisualStateManager.GoToState(
            this,
            this.newNameIsValid ? NameIsValidVisualState : NameIsInvalidVisualState,
            useTransitions: true);
    }

    private void RenameTextBox_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        var textBox = (TextBox)sender;

#pragma warning disable IDE0010 // Add missing cases

        // ReSharper disable once SwitchStatementMissingSomeEnumCasesNoDefault
        switch (e.Key)
        {
            case VirtualKey.Escape:
                textBox.LostFocus -= this.RenameTextBox_LostFocus;
                this.CancelRename();
                e.Handled = true;
                break;
            case VirtualKey.Enter:
                if (this.newNameIsValid)
                {
                    this.TryCommitRename();
                    e.Handled = true;
                }

                break;
            case VirtualKey.Up:
                if (!IsShiftKeyDown())
                {
                    textBox.SelectionStart = 0;
                }

                e.Handled = true;
                break;
            case VirtualKey.Down:
                if (!IsShiftKeyDown())
                {
                    textBox.SelectionStart = textBox.Text.Length;
                }

                e.Handled = true;
                break;
            case VirtualKey.Left:
                e.Handled = textBox.SelectionStart == 0;
                break;
            case VirtualKey.Right:
                e.Handled = textBox.SelectionStart + textBox.SelectionLength == textBox.Text.Length;
                break;
        }
#pragma warning restore IDE0010 // Add missing cases
    }

    private void RenameTextBox_ContextMenuOpening(object sender, ContextMenuEventArgs e)
        => this.isContextMenuOpen = true;

    private void RenameTextBox_GotFocus(object sender, RoutedEventArgs e) => this.isContextMenuOpen = false;

    private void SetRenameError(string? message)
    {
        if (this.itemNameErrorGlyph is { } errorGlyph)
        {
            ToolTipService.SetToolTip(errorGlyph, message);
            AutomationProperties.SetName(errorGlyph, string.IsNullOrWhiteSpace(message) ? string.Empty : message);
        }
    }
}

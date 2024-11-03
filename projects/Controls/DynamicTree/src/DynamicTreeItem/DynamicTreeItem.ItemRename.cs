// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Diagnostics;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

/// <summary>
/// Contains the implementation details of the item name in-place editing.
/// </summary>
public partial class DynamicTreeItem
{
    private ContentPresenter? itemContentPart;
    private TextBlock? itemNameTextBlock;
    private TextBox? itemNameTextBox;
    private string? oldItemName;
    private bool isContextMenuOpen;
    private bool newNameIsValid;

    private void SetupItemNameParts()
    {
        if (this.itemNameTextBlock is not null)
        {
            this.itemNameTextBlock.DoubleTapped -= this.StartRenameItem;
        }

        this.itemContentPart = this.GetTemplateChild(ContentPresenterPart) as ContentPresenter;
        if (this.itemContentPart is null)
        {
            return;
        }

        this.itemNameTextBlock = this.GetTemplateChild(ItemNamePart) as TextBlock;
        var inPlaceRenamePart = this.GetTemplateChild(InPlaceRenamePart) as Grid;
        this.itemNameTextBox = inPlaceRenamePart?.FindDescendant<TextBox>(
            x => string.Equals(x.Name, ItemNameEditPart, StringComparison.Ordinal));
        if (this.itemNameTextBlock is not null && this.itemNameTextBox is not null)
        {
            this.itemNameTextBlock.DoubleTapped += this.StartRenameItem;
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

    private void TryCommitRename()
    {
        Debug.Assert(
            this.itemNameTextBox is not null,
            "event handler should not be setup if parts are missing");

        // Update the TreeItemAdapter model object, which could validate the new name and
        // eventually reject it.
        try
        {
            this.ItemAdapter!.Label = this.itemNameTextBox.Text.Trim();
            this.EndRename();
        }
        catch
        {
            var success = VisualStateManager.GoToState(this, "Invalid", useTransitions: true);
            if (!success)
            {
                this.CancelRename();
            }
        }
    }

    private void StartRenameItem(object sender, DoubleTappedRoutedEventArgs e)
    {
        e.Handled = true;

        Debug.Assert(
            this.itemNameTextBlock is not null && this.itemNameTextBox is not null,
            "event handler should not be setup if parts are missing");

        this.itemNameTextBox.Text = this.itemNameTextBlock.Text;
        this.oldItemName = this.itemNameTextBlock.Text;
        this.itemNameTextBlock.Visibility = Visibility.Collapsed;
        this.itemNameTextBox.Visibility = Visibility.Visible;

        this.itemNameTextBox.TextChanged += this.RenameTextBox_TextChanged;
        this.itemNameTextBox.LostFocus += this.RenameTextBox_LostFocus;
        this.itemNameTextBox.KeyDown += this.RenameTextBox_KeyDown;
        this.itemNameTextBox.GotFocus += this.RenameTextBox_GotFocus;
        this.itemNameTextBox.ContextMenuOpening += this.RenameTextBox_ContextMenuOpening;

        _ = this.itemNameTextBox.Focus(FocusState.Programmatic);

        this.itemNameTextBox.SelectAll();
    }

    private void EndRename()
    {
        Debug.Assert(
            this.itemNameTextBlock is not null && this.itemNameTextBox is not null,
            "event handler should not be setup if parts are missing");

        this.itemNameTextBox.Visibility = Visibility.Collapsed;
        this.itemNameTextBlock.Visibility = Visibility.Visible;

        // Unsubscribe from events
        this.itemNameTextBox.TextChanged -= this.RenameTextBox_TextChanged;
        this.itemNameTextBox.LostFocus -= this.RenameTextBox_LostFocus;
        this.itemNameTextBox.KeyDown -= this.RenameTextBox_KeyDown;
        this.itemNameTextBox.GotFocus -= this.RenameTextBox_GotFocus;
        this.itemNameTextBox.ContextMenuOpening -= this.RenameTextBox_ContextMenuOpening;

        // Re-focus selected list item
        this.Focus(FocusState.Programmatic);
    }

    private void CancelRename()
    {
        this.itemNameTextBox!.Text = this.oldItemName;
        this.EndRename();
        _ = VisualStateManager.GoToState(this, NameIsValidVisualState, useTransitions: true);
    }

    private void RenameTextBox_TextChanged(object sender, TextChangedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.newNameIsValid = this.ItemAdapter!.ValidateItemName(this.itemNameTextBox!.Text);
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
                    textBox.LostFocus -= this.RenameTextBox_LostFocus;
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
}

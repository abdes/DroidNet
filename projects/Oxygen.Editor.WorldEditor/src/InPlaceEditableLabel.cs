// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Core;

namespace Oxygen.Editor.WorldEditor;

[TemplatePart(Name = EntityNameTextBlockPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = RenameTextBoxPartName, Type = typeof(TextBox))]
[TemplatePart(Name = EntityNameErrorPartName, Type = typeof(FontIcon))]
[TemplateVisualState(Name = NormalStateName, GroupName = EditingVisualStatesGroupName)]
[TemplateVisualState(Name = EditingStateName, GroupName = EditingVisualStatesGroupName)]
[TemplateVisualState(Name = InvalidValueStateName, GroupName = EditingVisualStatesGroupName)]
public class InPlaceEditableLabel : ContentControl
{
    public const string EntityNameTextBlockPartName = "PartEntityNameTextBlock";
    public const string RenameTextBoxPartName = "PartRenameTextBox";
    public const string EntityNameErrorPartName = "PartEntityNameError";

    public const string EditingVisualStatesGroupName = "EditingVisualStates";
    public const string NormalStateName = "Normal";
    public const string EditingStateName = "Editing";
    public const string InvalidValueStateName = "InvalidValue";

    public static readonly DependencyProperty TextProperty =
        DependencyProperty.Register(
            nameof(Text),
            typeof(string),
            typeof(InPlaceEditableLabel),
            new PropertyMetadata(
                defaultValue: null,
                (d, _) => ((InPlaceEditableLabel)d).UpdateDisplayText()));

    public static readonly DependencyProperty DisplayTextProperty =
        DependencyProperty.Register(
            nameof(DisplayText),
            typeof(string),
            typeof(InPlaceEditableLabel),
            new PropertyMetadata(default));

    private bool isEditing;
    private TextBlock? entityNameTextBlock;
    private TextBox? renameTextBox;
    private string? originalName;
    private bool newNameIsValid;

    public string? Text
    {
        get => (string?)this.GetValue(TextProperty);
        set => this.SetValue(TextProperty, value);
    }

    public string DisplayText
    {
        get => (string)this.GetValue(DisplayTextProperty);
        set => this.SetValue(DisplayTextProperty, value);
    }

    public InPlaceEditableLabel()
    {
        this.DefaultStyleKey = typeof(InPlaceEditableLabel);
    }

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.entityNameTextBlock = this.GetTemplateChild(EntityNameTextBlockPartName) as TextBlock;
        this.renameTextBox = this.GetTemplateChild(RenameTextBoxPartName) as TextBox;

        if (this.entityNameTextBlock != null)
        {
            this.entityNameTextBlock.DoubleTapped += this.EntityNameTextBlock_DoubleTapped;
        }

        if (this.renameTextBox != null)
        {
            // Register event handlers once
            this.renameTextBox.GotFocus += this.RenameTextBox_GotFocus;
            this.renameTextBox.KeyDown += this.RenameTextBox_KeyDown;
            this.renameTextBox.LostFocus += this.RenameTextBox_LostFocus;
            this.renameTextBox.TextChanged += this.RenameTextBox_TextChanged;
        }

        this.UpdateDisplayText();
        this.UpdateVisualState();
    }

    private void UpdateDisplayText()
    {
        // Update the value of the DisplayText dependency property
        var displayText = string.IsNullOrEmpty(this.Text) ? "Multiple Values" : this.Text!;
        this.SetValue(DisplayTextProperty, displayText);
    }

    private void UpdateVisualState(bool useTransitions = true)
        => _ = this.isEditing
            ? this.newNameIsValid
                ? VisualStateManager.GoToState(this, EditingStateName, useTransitions)
                : VisualStateManager.GoToState(this, InvalidValueStateName, useTransitions)
            : VisualStateManager.GoToState(this, NormalStateName, useTransitions);

    private void EntityNameTextBlock_DoubleTapped(object sender, DoubleTappedRoutedEventArgs e)
    {
        e.Handled = true;
        this.StartRename();
    }

    private void StartRename()
    {
        if (this.renameTextBox is null)
        {
            return;
        }

        this.originalName = this.Text;
        this.ValidateName();
        this.isEditing = true;

        this.UpdateVisualState();

        _ = this.renameTextBox.Focus(FocusState.Programmatic);
    }

    private void EndRename()
    {
        this.isEditing = false;
        this.UpdateVisualState();
    }

    private void TryCommitRename()
    {
        Debug.Assert(this.newNameIsValid, "new name must be valid before trying to commit");

        if (this.renameTextBox != null)
        {
            var newName = this.renameTextBox.Text.Trim();
            if (this.originalName?.Equals(newName, StringComparison.Ordinal) == false)
            {
                this.Text = newName;
            }
        }

        this.EndRename();
    }

    private void CancelRename()
    {
        if (this.renameTextBox is not null)
        {
            this.renameTextBox.Text = this.originalName;
        }

        this.EndRename();
    }

    private void RenameTextBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        this.ValidateName();
        this.UpdateVisualState(false);
    }

    private void ValidateName()
    {
        if (this.renameTextBox is not null)
        {
            this.newNameIsValid = InputValidation.IsValidFileName(this.renameTextBox.Text.Trim());
        }
    }

    private void RenameTextBox_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        switch (e.Key)
        {
            case Windows.System.VirtualKey.Enter when !this.newNameIsValid:
                return;
            case Windows.System.VirtualKey.Enter:
                this.TryCommitRename();
                e.Handled = true;
                break;

            case Windows.System.VirtualKey.Escape:
                this.CancelRename();
                e.Handled = true;
                break;
        }
    }

    private void RenameTextBox_LostFocus(object sender, RoutedEventArgs e)
    {
        if (this.newNameIsValid)
        {
            this.TryCommitRename();
        }
        else
        {
            this.CancelRename();
        }
    }

    private void RenameTextBox_GotFocus(object sender, RoutedEventArgs e) => this.renameTextBox?.SelectAll();
}
